/* ZType-style typing shooter. Game logic and rendering use the 480x720 portrait
 * coordinate space of the original; everything is scaled by 2/3 onto the 300x450
 * playfield column of the 640x480 screen. All artwork is generated procedurally. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "gfx.h"
#include "game.h"
#include "sound.h"

#define NW 480.f
#define NH 720.f
#define KS (FW / 480.f)
#define PI_F 3.14159265f

static float shx, shy;
#define SX(x) (FX + ((x) + shx) * KS)
#define SY(y) (FY + ((y) + shy) * KS)

/* ---------------- helpers ---------------- */
static uint32_t rs = 0x2545F491u;
static float frand(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (rs & 0xFFFFFF) / 16777216.f; }
static int irand(int n) { return (int)(frand() * n); }
static float mapf(float v, float a0, float a1, float b0, float b1) { return b0 + (v - a0) * (b1 - b0) / (a1 - a0); }
static float easeIO(float t, float b, float c, float d) {
	t /= d * .5f;
	if (t < 1) return c * .5f * t * t + b;
	t--;
	return -c * .5f * (t * (t - 2) - 1) + b;
}
static void zfill(char *buf, int n, int v) { snprintf(buf, 16, "%0*d", n, v); }

/* ---------------- persistence ---------------- */
#define SV_DIR "sd:/apps/typeshot"
#define SV_FILE SV_DIR "/highscore.dat"
#define SV_TEXT SV_DIR "/text.txt"
#define HIST 16
static int sdOK;
static struct { int best, games, bestWave, bestStreak, accSum, nHist, hist[HIST]; } sv;

static unsigned svSum(void) {
	unsigned h = 2166136261u;
	int v[6] = {sv.best, sv.games, sv.bestWave, sv.bestStreak, sv.accSum, sv.nHist};
	for (int i = 0; i < 6; i++) h = (h ^ (unsigned)v[i]) * 16777619u;
	for (int i = 0; i < sv.nHist; i++) h = (h ^ (unsigned)sv.hist[i]) * 16777619u;
	return h;
}
static void svLoad(void) {
	memset(&sv, 0, sizeof sv);
	if (!sdOK) return;
	FILE *f = fopen(SV_FILE, "r");
	if (!f) return;
	unsigned c;
	char tag[8];
	if (fscanf(f, "%7s", tag) == 1 && !strcmp(tag, "ZT2")) {
		if (fscanf(f, "%d %d %d %d %d %d", &sv.best, &sv.games, &sv.bestWave, &sv.bestStreak, &sv.accSum, &sv.nHist) == 6 &&
			sv.nHist >= 0 && sv.nHist <= HIST) {
			for (int i = 0; i < sv.nHist; i++) if (fscanf(f, "%d", &sv.hist[i]) != 1) sv.nHist = 0;
			if (fscanf(f, "%u", &c) != 1 || c != svSum()) memset(&sv, 0, sizeof sv);
		} else memset(&sv, 0, sizeof sv);
	} else if (!strcmp(tag, "TSHOT1")) {
		unsigned v;
		if (fscanf(f, "%u %u", &v, &c) == 2 && c == ((v * 2654435761u) ^ 0x5459u)) sv.best = (int)v;
	}
	fclose(f);
}
static void svSave(void) {
	if (!sdOK) return;
	mkdir("sd:/apps", 0777);
	mkdir(SV_DIR, 0777);
	FILE *f = fopen(SV_FILE, "w");
	if (!f) return;
	fprintf(f, "ZT2 %d %d %d %d %d %d", sv.best, sv.games, sv.bestWave, sv.bestStreak, sv.accSum, sv.nHist);
	for (int i = 0; i < sv.nHist; i++) fprintf(f, " %d", sv.hist[i]);
	fprintf(f, " %u\n", svSum());
	fclose(f);
}

/* ---------------- words ---------------- */
static const char *WORDS[] = {
 "go","up","if","it","me","we","he","be","do","so","no","on","to","an","my","by","at","as","or","in","ox","us",
 "sun","sky","run","fox","hat","map","key","ice","owl","bee","jet","arc","gas","ray","orb","zip","web","fan","lip",
 "rug","dig","cup","fog","gem","hub","ink","jam","kit","log","mud","nut","oak","pen","quiz","rim","sea","tap","urn","van","wax","yak",
 "star","moon","dust","wind","fire","gold","rain","ship","dark","void","wave","ring","bolt","core","flux","grid","hope","iron",
 "jump","keen","lava","mind","nova","open","path","quit","rock","salt","tide","unit","vast","warp","yarn","zone","lock","this","fall","glow",
 "laser","orbit","comet","probe","pulse","flare","light","space","solar","alien","robot","storm","blast","drift","sweep","ghost",
 "noble","quick","razor","frost","magic","shift","titan","ultra","vapor","worry","spring","north","crash","dream","eagle","force",
 "rocket","shield","plasma","galaxy","engine","thrust","cosmos","cannon","fusion","meteor","photon","quasar","vector","signal",
 "planet","system","energy","beacon","hunter","mirror","nebula","shadow","silver","tunnel","vortex","winter","bright","castle","dragon",
 "cluster","reactor","gravity","phantom","station","capsule","eclipse","mission","command","fighter","gunship","horizon","journey",
 "lantern","mystery","network","orbital","pioneer","quantum","rainbow","sunrise","thunder","venture","anomaly","balance","courage",
 "asteroid","starship","universe","hurricane","absolute","guardian","hydrogen","infinite","momentum","navigate","overload","particle",
 "radiance","scramble","terminal","vanguard","wanderer","lightning","adventure","broadcast","countdown","defensive","emergency",
 "frontline","generator","hyperdrive","intercept","landscape","telescope","satellite","supernova","spacewalk","wormhole","reentry",
 "starlight","battleship","antimatter","atmosphere","collision","detonation","exploration","hypothetical","overwhelming",
 "propulsion","reconnaissance","navigation","bombardment","constellation","interference","acceleration","extraterrestrial", 0
};
static const char *byLen[16][96];
static int nByLen[16];
static char **custom;
static int nCustom, customOn, customQ;
static char msg[64];
static float msgT;

static void bucketWords(void) {
	for (int i = 0; WORDS[i]; i++) {
		int l = (int)strlen(WORDS[i]);
		if (l >= 2 && l <= 12 && nByLen[l] < 96) byLen[l][nByLen[l]++] = WORDS[i];
	}
}

static int loadText(void) {
	if (!sdOK) return -1;
	FILE *f = fopen(SV_TEXT, "r");
	if (!f) return -1;
	for (int i = 0; i < nCustom; i++) free(custom[i]);
	free(custom); custom = NULL; nCustom = 0;
	char w[32]; int n = 0, c;
	custom = malloc(sizeof(char *) * 2048);
	while ((c = fgetc(f)) != EOF) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { if (n < 15) w[n++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c); else n = 16; }
		else if (c != '\'') {
			if (n >= 2 && n <= 15 && nCustom < 2048) { w[n] = 0; custom[nCustom++] = strdup(w); }
			n = 0;
		}
	}
	if (n >= 2 && n <= 15 && nCustom < 2048) { w[n] = 0; custom[nCustom++] = strdup(w); }
	fclose(f);
	return nCustom;
}

/* ---------------- game state ---------------- */
enum { MODE_TITLE, MODE_GAME, MODE_OVER };
enum { MENU_NONE, MENU_TITLE, MENU_SETTINGS, MENU_OVER, MENU_STATS, MENU_ABOUT };
enum { T_MINE, T_MISSILE, T_DESTROYER, T_OPPRESSOR, T_BULLET };

static const struct { float w, h, speed, sheetW, sheetH; int wmin, wmax, nkill, nhit; } TY[5] = {
	{12, 12, 30, 32, 32, 3, 6, 20, 5},
	{8, 15, 35, 20, 26, 2, 5, 10, 2},
	{24, 34, 20, 43, 58, 7, 10, 40, 10},
	{36, 58, 15, 68, 88, 9, 12, 40, 10},
	{2, 2, 50, 20, 24, 1, 1, 20, 5},
};

typedef struct {
	int on, type;
	float x, y, vx, vy, angle, spin, hitT, retT, shootT, tT;
	char word[20];
	int len, rem, targeted, dead, health;
} Ent;
typedef struct { int on, target; float x, y, vx, vy, dist; } Plasma;
typedef struct { int on, spark; float x, y, vx, vy, t, rot; } Part;
typedef struct { int on; float t, x, y; int glitch; } Boom;

#define MAXE 96
#define MAXP 64
#define MAXPT 700
static Ent en[MAXE];
static Plasma pl[MAXP];
static Part pt[MAXPT];
static Boom boom[6];
static struct { int on; float t, x, y; } emp;

static int mode = MODE_TITLE, menu = MENU_TITLE, cur;
static float tnow, menuT;
static int quit, kbOn;

static int score, streak, longestStreak, hits, misses, multiplier, kills, emps, wave, isBest, target;
static float speedFactor, spawnWait, curSpawnWait, spawnT, idleT, waveEndT, killFlash, shake, yScroll, yScroll2, multiT, gameT;
static int multiIdx;
static int waveEnd;
static struct { int type, count, incEvery; } WT[3];
typedef struct { int type; char word[20]; } Spawn;
static Spawn spawnList[160];
static int nSpawn;
static struct { int alive; float x, y, angle, targetAngle, flashT, missT; } pl1;
static float gameTransT; static int gameTrans;
static float overT;
static float volSfx = .5f, volMusic = .5f;
static int overShown;

/* ---------------- background art ---------------- */
static uint32_t bgPage[SH][SW];
static uint32_t nebula[FH][FW];
static uint32_t gbg[FH][FW];
static uint32_t tmpField[FH][FW];

static float lat[5][34][34];
static float vnoise(int o, float x, float y, int sx, int sy) {
	int x0 = (int)floorf(x), y0 = (int)floorf(y);
	float fx = x - x0, fy = y - y0;
	fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
	int xa = ((x0 % sx) + sx) % sx, xb = (xa + 1) % sx, ya = ((y0 % sy) + sy) % sy, yb = (ya + 1) % sy;
	float a = lat[o][ya][xa], b = lat[o][ya][xb], c = lat[o][yb][xa], d = lat[o][yb][xb];
	return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}

static void makeArt(void) {
	/* page: dark diamond lattice + soft shadow around the playfield */
	for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++) {
		int u = (int)floorf((x + y) / 320.f), v = (int)floorf((x - y) / 320.f);
		int dark = ((u + v) & 1) != 0;
		int r = dark ? 21 : 27, g = dark ? 21 : 27, b = dark ? 20 : 24;
		float dx = x < FX ? FX - x : x > FX + FW ? x - FX - FW : 0;
		float dy = y < FY ? FY - y : y > FY + FH ? y - FY - FH : 0;
		float d = sqrtf(dx * dx + dy * dy);
		if (d < 30) { float s = 1.f - d / 30.f; float k = 1.f - .55f * s * s; r = (int)(r * k); g = (int)(g * k); b = (int)(b * k); }
		bgPage[y][x] = RGB(r, g, b);
	}
	/* nebula tile (wraps vertically) */
	for (int o = 0; o < 5; o++) for (int j = 0; j < 34; j++) for (int i = 0; i < 34; i++) lat[o][j][i] = frand();
	static const int cells[5] = {3, 6, 12, 24, 32};
	for (int y = 0; y < FH; y++) for (int x = 0; x < FW; x++) {
		float n1 = 0, a = .5f, tot = 0;
		float n2 = 0;
		for (int o = 0; o < 4; o++) {
			int sx = cells[o] * 2 / 3 + 2, sy = cells[o];
			if (sx > 33) sx = 33;
			float fx = (float)x / FW * sx, fy = (float)y / FH * sy;
			n1 += a * vnoise(o, fx, fy, sx, sy);
			n2 += a * vnoise(4, fx * .5f + 3.f * o, fy * .5f + 2.f * o, 16, 16);
			tot += a; a *= .5f;
		}
		n1 /= tot; n2 /= tot;
		float t = clampf((n1 - .38f) * 2.4f, 0.f, 1.f); t = t * t;
		float p = clampf((n2 - .45f) * 3.f, 0.f, 1.f) * t;
		float r = 3 + t * 12 + p * 40, g = 4 + t * 13 + p * 6, b = 11 + t * 34 + p * 42;
		float grain = (frand() - .5f) * 5.f;
		nebula[y][x] = RGB(clampi((int)(r + grain), 0, 255), clampi((int)(g + grain), 0, 255), clampi((int)(b + grain), 0, 255));
	}
	for (int i = 0; i < 1100; i++) {
		int x = irand(FW), y = irand(FH);
		float br = frand(); br = br * br;
		int v = 50 + (int)(br * 190);
		int tint = irand(5);
		int r = v, g = v, b = v;
		if (tint == 0) { r = v * 3 / 4; g = v * 7 / 8; } else if (tint == 1) { b = v * 3 / 4; g = v * 7 / 8; }
		uint32_t c = RGB(r, g, b);
		nebula[y][x] = c;
		if (br > .7f) {
			uint32_t h = RGB(r / 3, g / 3, b / 3);
			nebula[y][(x + 1) % FW] = h; nebula[y][(x + FW - 1) % FW] = h;
			nebula[(y + 1) % FH][x] = h; nebula[(y + FH - 1) % FH][x] = h;
		}
	}
	/* in-game backdrop: faint stars under a dark teal gradient */
	for (int y = 0; y < FH; y++) for (int x = 0; x < FW; x++) {
		float vy = (float)y / FH, vx = (float)x / FW - .5f;
		float vig = 1.f - .55f * (vx * vx * 3.f + (vy - .45f) * (vy - .45f) * 1.3f);
		vig = clampf(vig, .25f, 1.f);
		float r = 9 + 8 * vig, g = 20 + 16 * vig, b = 24 + 14 * vig;
		uint32_t n = nebula[y][x];
		gbg[y][x] = RGB(clampi((int)(r + CR(n) * .22f), 0, 255), clampi((int)(g + CG(n) * .22f), 0, 255), clampi((int)(b + CB(n) * .22f), 0, 255));
	}
}

static void drawPageText(void) {
	static const char *parts[] = {"ZType (c)", " - ", "phoboslab.org", " - ", "feedback", " - ", "soundtrack"};
	static const uint32_t cols[] = {RGB(95, 95, 90), RGB(95, 95, 90), RGB(150, 150, 144), RGB(95, 95, 90), RGB(150, 150, 144), RGB(95, 95, 90), RGB(150, 150, 144)};
	float w = 0;
	for (int i = 0; i < 7; i++) w += text_w(fFoot, parts[i], 0);
	float x = SW - 6 - w;
	for (int i = 0; i < 7; i++) { text(fFoot, x, SH - 14, parts[i], cols[i], 255, 0, AL_LEFT); x += text_w(fFoot, parts[i], 0); }
}

static void blitField(uint32_t (*src)[FW], int scroll) {
	scroll = ((scroll % FH) + FH) % FH;
	for (int y = 0; y < FH; y++) memcpy(&fb[FY + y][FX], src[(y + FH - scroll) % FH], FW * 4);
}

/* ---------------- sprite helpers ---------------- */
static void spoly(float cx, float cy, float rot, float sc, const float *p, int n, uint32_t c, int a, int add) {
	float pts[64], co = cosf(rot), si = sinf(rot);
	for (int i = 0; i < n; i++) {
		float x = p[i * 2] * sc, y = p[i * 2 + 1] * sc;
		pts[i * 2] = SX(cx + x * co - y * si);
		pts[i * 2 + 1] = SY(cy + x * si + y * co);
	}
	int cnt[1] = {n};
	poly_fill(pts, cnt, 1, c, a, add, NULL, NULL);
}
static void soutline(float cx, float cy, float rot, float sc, const float *p, int n, uint32_t c, int a) {
	float co = cosf(rot), si = sinf(rot);
	for (int i = 0; i < n; i++) {
		int j = (i + 1) % n;
		float x0 = p[i * 2] * sc, y0 = p[i * 2 + 1] * sc, x1 = p[j * 2] * sc, y1 = p[j * 2 + 1] * sc;
		line_aa(SX(cx + x0 * co - y0 * si), SY(cy + x0 * si + y0 * co), SX(cx + x1 * co - y1 * si), SY(cy + x1 * si + y1 * co), 1.1f, c, a, 1);
	}
}
static void sglow(float x, float y, float rad, uint32_t c, float k) { glow(SX(x), SY(y), rad * KS, c, k); }

static const float SHIP[] = {0,-13, 4,-4, 11,8, 5,6, 0,9, -5,6, -11,8, -4,-4};
static void drawShip(float cx, float cy, float ang, int title, float flash, float miss) {
	uint32_t body = title ? RGB(120, 62, 22) : RGB(18, 100, 106);
	uint32_t edge = title ? RGB(255, 165, 90) : RGB(110, 255, 232);
	if (miss > 0) { edge = RGB(255, 90, 70); body = RGB(110, 24, 18); }
	spoly(cx, cy, ang, 1.f, SHIP, 8, body, 190, 1);
	soutline(cx, cy, ang, 1.f, SHIP, 8, edge, 230);
	float co = cosf(ang), si = sinf(ang);
	line_aa(SX(cx + (-3) * co - (-3) * si), SY(cy + (-3) * si + (-3) * co), SX(cx + 3 * co - (-3) * si), SY(cy + 3 * si + (-3) * co), 1.f, edge, 140, 1);
	float fl = 8 + 3 * sinf(tnow * 40.f) + flash * 6;
	sglow(cx - 10 * si, cy + 10 * co, fl, title ? RGB(255, 120, 40) : RGB(60, 220, 255), .8f);
	sglow(cx + 4 * si, cy - 5 * co, 7 + flash * 10, title ? RGB(255, 190, 120) : RGB(180, 255, 250), .55f + flash * .6f);
}

static void drawEnemySprite(Ent *e) {
	float cx = e->x + TY[e->type].w * .5f, cy = e->y + TY[e->type].h * .5f;
	switch (e->type) {
	case T_MINE: {
		float r = e->spin;
		sglow(cx, cy, 15, RGB(255, 100, 20), .55f);
		ring(SX(cx), SY(cy), 6.5f * KS * 1.1f, 1.6f, RGB(255, 150, 50), 1.f);
		for (int k = 0; k < 4; k++) {
			float a = r + k * PI_F * .5f;
			line_aa(SX(cx + cosf(a) * 6), SY(cy + sinf(a) * 6), SX(cx + cosf(a) * 11), SY(cy + sinf(a) * 11), 1.2f, RGB(255, 170, 70), 230, 1);
		}
		disc(SX(cx), SY(cy), 2.4f * KS * 1.3f, RGB(255, 230, 180), 255);
		break; }
	case T_MISSILE: {
		static const float P[] = {0,10, 4,-6, 0,-3, -4,-6};
		float rot = e->angle - PI_F * .5f;
		spoly(cx, cy, rot, 1.f, P, 4, RGB(150, 50, 30), 200, 1);
		soutline(cx, cy, rot, 1.f, P, 4, RGB(255, 140, 90), 230);
		sglow(cx - cosf(e->angle) * 9, cy - sinf(e->angle) * 9, 9, RGB(255, 140, 40), .9f);
		break; }
	case T_DESTROYER:
	case T_OPPRESSOR: {
		static const float P[] = {0,26, 7,10, 18,0, 20,-12, 10,-26, 0,-22, -10,-26, -20,-12, -18,0, -7,10};
		static const float IN[] = {0,18, 5,4, 0,-10, -5,4};
		float rot = e->angle - PI_F * .5f, sc = e->type == T_OPPRESSOR ? 1.45f : 1.f;
		uint32_t edge = e->type == T_OPPRESSOR ? RGB(255, 140, 120) : RGB(150, 195, 240);
		uint32_t body = e->type == T_OPPRESSOR ? RGB(90, 36, 36) : RGB(44, 62, 94);
		spoly(cx, cy, rot, sc, P, 10, body, 200, 1);
		spoly(cx, cy, rot, sc, IN, 4, RGB(20, 28, 44), 160, 1);
		soutline(cx, cy, rot, sc, P, 10, edge, 230);
		float co = cosf(rot), si = sinf(rot);
		for (int k = -1; k <= 1; k += 2) {
			float lx = 12.f * k * sc, ly = -4.f * sc;
			sglow(cx + lx * co - ly * si, cy + lx * si + ly * co, 7, RGB(255, 120, 40), .9f);
			lx = 6.f * k * sc; ly = -24.f * sc;
			sglow(cx + lx * co - ly * si, cy + lx * si + ly * co, 9, RGB(255, 150, 60), .8f);
		}
		break; }
	default:
		sglow(cx, cy, 11, RGB(255, 120, 40), .9f);
		disc(SX(cx), SY(cy), 2.2f, RGB(255, 235, 200), 255);
		break;
	}
}

/* ---------------- entities ---------------- */
static float dist2player(Ent *e) {
	float dx = e->x + TY[e->type].w * .5f - (pl1.x + 4), dy = e->y + TY[e->type].h * .5f - (pl1.y + 4);
	return sqrtf(dx * dx + dy * dy);
}
static float angleToPlayer(Ent *e) {
	return atan2f(pl1.y + 4 - (e->y + TY[e->type].h * .5f), pl1.x + 4 - (e->x + TY[e->type].w * .5f));
}
static int firstLetterTaken(char c) {
	for (int i = 0; i < MAXE; i++) if (en[i].on && !en[i].targeted && !en[i].dead && en[i].rem < en[i].len && en[i].word[en[i].rem] == c) return 1;
	return 0;
}
static const char *pickWord(int len) {
	static char one[2];
	const char *w = NULL;
	for (int t = 0; t < 20; t++) {
		if (len >= 2 && len <= 12 && nByLen[len]) w = byLen[len][irand(nByLen[len])];
		else { one[0] = (char)('a' + irand(26)); one[1] = 0; w = one; }
		if (!firstLetterTaken(w[0])) return w;
	}
	return w;
}
static void shake_(float s) { if (s > shake) shake = s; }

static Ent *newEnt(int type, float x, float y, const char *word, float angle, int useAngle) {
	for (int i = 0; i < MAXE; i++) if (!en[i].on) {
		Ent *e = &en[i];
		memset(e, 0, sizeof *e);
		e->on = 1; e->type = type; e->x = x; e->y = y;
		if (!word) {
			int len = (int)floorf(mapf(frand(), 0, 1, (float)TY[type].wmin, (float)TY[type].wmax) + .5f);
			word = pickWord(len);
		}
		strncpy(e->word, word, 15);
		e->len = (int)strlen(e->word);
		e->health = e->len;
		e->angle = angleToPlayer(e);
		if (useAngle) e->angle = angle;
		e->tT = 1.f;
		if (type == T_DESTROYER) {
			e->shootT = 5.f;
			e->angle = (mapf(frand(), 0, 1, 67, 90) + (x > NW * .5f ? 22.5f : 0)) * PI_F / 180.f;
		}
		if (type == T_OPPRESSOR) { e->shootT = 7.f; e->angle = PI_F * .5f; }
		return e;
	}
	return NULL;
}

static void spawnPart(float x, float y, int spark, float vmax) {
	for (int i = 0; i < MAXPT; i++) if (!pt[i].on) {
		pt[i].on = 1; pt[i].spark = spark; pt[i].x = x; pt[i].y = y;
		pt[i].vx = (frand() * 2 - 1) * vmax; pt[i].vy = (frand() * 2 - 1) * vmax;
		pt[i].t = 0; pt[i].rot = frand() * 6.28f;
		return;
	}
}
static void explodeParts(Ent *e, int killed) {
	float cx = e->x + TY[e->type].w * .5f, cy = e->y + TY[e->type].h * .5f;
	int n = killed ? TY[e->type].nkill : TY[e->type].nhit;
	for (int i = 0; i < n; i++) spawnPart(cx, cy, 0, 60.f);
	for (int i = 0; i < n / 2; i++) spawnPart(cx, cy, 1, 60.f);
}
static void addBoom(float x, float y, int glitch) {
	for (int i = 0; i < 6; i++) if (!boom[i].on) { boom[i].on = 1; boom[i].t = 0; boom[i].x = x; boom[i].y = y; boom[i].glitch = glitch; return; }
}
static void killEnt(Ent *e, int silent) {
	if (target >= 0 && &en[target] == e) target = -1;
	e->on = 0;
	if (silent) return;
	if (e->type == T_DESTROYER) { sound_play(SFX_EXPLODE_L); addBoom(e->x + 12, e->y + 22, 0); shake_(10); }
	else if (e->type == T_OPPRESSOR) { sound_play(SFX_EXPLODE_L); addBoom(e->x + 12, e->y + 22, 0); shake_(20); }
	else { sound_play(SFX_EXPLODE); if (e->type == T_MINE) shake_(5); }
	killFlash = .3f;
	float cx = e->x + TY[e->type].w * .5f, cy = e->y + TY[e->type].h * .5f;
	for (int i = 0; i < TY[e->type].wmax; i++) spawnPart(cx, cy, 1, 360.f);
}
static void hitEnt(Ent *e) {
	explodeParts(e, e->health <= 1);
	float a = e->angle;
	e->vx = -cosf(a) * 20 + frand() * 20; e->vy = -sinf(a) * 20 + frand() * 20;
	e->hitT = .3f; killFlash = .2f;
	e->health--;
	sound_play(SFX_HIT);
	if (e->health <= 0) killEnt(e, 0);
}

/* ---------------- waves ---------------- */
static const char *nextCustom(void) {
	static int *q; static int qn, qi;
	if (!q) q = malloc(sizeof(int) * 2048);
	if (qi >= qn) {
		qn = nCustom; qi = 0;
		for (int i = 0; i < qn; i++) q[i] = i;
		for (int i = qn - 1; i > 0; i--) { int j = irand(i + 1), t = q[i]; q[i] = q[j]; q[j] = t; }
	}
	(void)customQ;
	return custom[q[qi++]];
}
static void nextWave(void) {
	wave++;
	spawnWait = clampf(spawnWait * .97f, .2f, 1.f);
	curSpawnWait = spawnWait;
	speedFactor *= 1.05f;
	nSpawn = 0;
	int dec = 0;
	for (int t = 0; t < 3; t++) {
		WT[t].count -= dec;
		if (wave % WT[t].incEvery == 0) { WT[t].count++; dec++; }
		for (int s = 0; s < WT[t].count && nSpawn < 160; s++) {
			spawnList[nSpawn].type = WT[t].type;
			spawnList[nSpawn].word[0] = 0;
			if (customOn && nCustom) strncpy(spawnList[nSpawn].word, nextCustom(), 15);
			nSpawn++;
		}
	}
	for (int i = nSpawn - 1; i > 0; i--) { int j = irand(i + 1); Spawn t = spawnList[i]; spawnList[i] = spawnList[j]; spawnList[j] = t; }
	spawnT = 0;
}

static void resetGame(void) {
	memset(en, 0, sizeof en); memset(pl, 0, sizeof pl); memset(pt, 0, sizeof pt); memset(boom, 0, sizeof boom);
	emp.on = 0; target = -1;
	score = streak = longestStreak = hits = misses = kills = wave = 0; multiplier = 1; isBest = 0;
	speedFactor = 1.f; spawnWait = .7f; curSpawnWait = .7f; idleT = 0; waveEnd = 0; waveEndT = 0; killFlash = 0; multiT = 0; gameT = 0;
	WT[0].type = T_OPPRESSOR; WT[0].count = 0; WT[0].incEvery = 7;
	WT[1].type = T_DESTROYER; WT[1].count = 0; WT[1].incEvery = 3;
	WT[2].type = T_MINE; WT[2].count = 3; WT[2].incEvery = 1;
	nSpawn = 0;
	pl1.x = NW * .5f - 6; pl1.y = 642; pl1.alive = 1; pl1.angle = pl1.targetAngle = 0; pl1.flashT = pl1.missT = 0;
}
static void setTitle(void) {
	resetGame();
	mode = MODE_TITLE; menu = MENU_TITLE; cur = 0; emps = 0; menuT = 0; gameTrans = 0; overShown = 0;
}
static void setGame(void) {
	resetGame();
	mode = MODE_GAME; emps = 3;
	pl1.y = 400; /* eases down during the intro */
	gameTrans = 1; gameTransT = 0;
	nextWave();
	menu = MENU_TITLE;
	sound_play(SFX_SPAWN);
}

static void gameOver(void) {
	mode = MODE_OVER; overT = 0; overShown = 0;
	if (score > sv.best) { sv.best = score; isBest = 1; }
	sv.games++;
	if (wave > sv.bestWave) sv.bestWave = wave;
	if (longestStreak > sv.bestStreak) sv.bestStreak = longestStreak;
	int acc = hits ? hits * 1000 / (hits + misses) : 0;
	sv.accSum += acc;
	if (sv.nHist == HIST) { memmove(sv.hist, sv.hist + 1, sizeof(int) * (HIST - 1)); sv.nHist--; }
	sv.hist[sv.nHist++] = score;
	svSave();
}
static void killPlayer(void) {
	if (!pl1.alive) return;
	pl1.alive = 0;
	sound_play(SFX_BREACH);
	addBoom(pl1.x + 12, pl1.y + 22, 1);
	shake_(10);
	gameOver();
}

/* ---------------- input ---------------- */
static void cancelTarget(void) {
	if (target >= 0) { en[target].targeted = 0; target = -1; sound_play(SFX_CANCEL); }
}
static void shootPlasma(int ti) {
	for (int i = 0; i < MAXP; i++) if (!pl[i].on) {
		pl[i].on = 1; pl[i].target = ti; pl[i].x = pl1.x + 6; pl[i].y = pl1.y + 4; pl[i].dist = 100000.f; pl[i].vx = pl[i].vy = 0;
		Ent *e = &en[ti];
		pl1.targetAngle = atan2f(e->y + TY[e->type].h * .5f - pl[i].y, e->x + TY[e->type].w * .5f - pl[i].x) + PI_F * .5f;
		pl1.flashT = .12f;
		sound_play(SFX_SHOOT);
		return;
	}
}
static void typeMiss(void) {
	pl1.missT = .15f; sound_play(SFX_MISS);
	multiplier = 1; streak = 0; misses++;
}
static void typeLetter(int c) {
	if (c >= 'A' && c <= 'Z') c += 32;
	if (c < 33 || c > 126) return;
	idleT = 0;
	if (target < 0) {
		float best = 1e9f; int bi = -1;
		for (int i = 0; i < MAXE; i++) {
			Ent *e = &en[i];
			if (!e->on || e->dead || e->targeted || e->rem >= e->len || e->word[e->rem] != c) continue;
			float d = dist2player(e);
			if (d < best) { best = d; bi = i; }
		}
		if (bi >= 0) { target = bi; en[bi].targeted = 1; en[bi].retT = .5f; sound_play(SFX_TARGET); }
		else { typeMiss(); return; }
	}
	Ent *t = &en[target];
	if (t->word[t->rem] == c) {
		int ti = target;
		t->rem++;
		shootPlasma(ti);
		score += multiplier; hits++; streak++;
		if (streak > longestStreak) longestStreak = streak;
		if (streak == 20 || streak == 50) { multiplier = streak == 20 ? 2 : 3; multiT = 2.f; multiIdx = multiplier; sound_play(SFX_MULTI); }
		if (t->rem >= t->len) { t->dead = 1; t->targeted = 0; target = -1; kills++; }
	} else typeMiss();
}

static void startEMP(void) {
	if (emps <= 0 || emp.on || !pl1.alive) return;
	emps--; emp.on = 1; emp.t = 0; emp.x = pl1.x + 4; emp.y = pl1.y + 4;
	sound_play(SFX_EMP); shake_(30);
}

void game_char(int c) {
	if (menu == MENU_TITLE && !gameTrans && (c == 'i' || c == 'I')) { menu = MENU_ABOUT; menuT = 0; return; }
	if (mode != MODE_GAME || menu != MENU_NONE) return;
	typeLetter(c);
}

static void menuNav(int k);
void game_key(int k) {
	if (menu != MENU_NONE) { menuNav(k); return; }
	if (mode != MODE_GAME) return;
	if (k == K_OK || k == K_EMP) startEMP();
	else if (k == K_BACK) cancelTarget();
	else if (k == K_MENU) { menu = MENU_SETTINGS; cur = 0; }
}
void game_set_keyboard(int p) { kbOn = p; }
int game_wants_quit(void) { return quit; }

/* ---------------- update ---------------- */
static void updateEnts(float dt) {
	float fr = shake; (void)fr;
	for (int i = 0; i < MAXE; i++) {
		Ent *e = &en[i];
		if (!e->on) continue;
		float spd = TY[e->type].speed * speedFactor;
		if (e->type == T_MINE) { e->angle = angleToPlayer(e); e->spin += 2.f * dt; }
		if (e->type == T_MISSILE) {
			e->tT -= dt;
			if (e->tT <= 0 && e->tT > -.7f) e->angle -= (e->angle - angleToPlayer(e)) * dt * 2.f;
		}
		e->hitT -= dt;
		if (e->hitT <= 0) { e->vx = cosf(e->angle) * spd; e->vy = sinf(e->angle) * spd; }
		else {
			float f = 100.f * dt;
			e->vx = e->vx > 0 ? fmaxf(0, e->vx - f) : fminf(0, e->vx + f);
			e->vy = e->vy > 0 ? fmaxf(0, e->vy - f) : fminf(0, e->vy + f);
		}
		e->x += e->vx * dt; e->y += e->vy * dt;
		if (e->retT > 0) e->retT -= dt;
		if (e->type == T_DESTROYER) {
			e->shootT -= dt;
			if (e->shootT <= 0) {
				e->shootT = 5.f;
				if (dist2player(e) > 100) { Ent *m = newEnt(T_MISSILE, e->x + 12, e->y + 22, NULL, e->angle, 1); (void)m; }
			}
		}
		if (e->type == T_OPPRESSOR) {
			e->shootT -= dt;
			if (e->shootT <= 0) {
				if (dist2player(e) > 100) {
					float a = 40.f, inc = 100.f / 7.f;
					for (int b = 0; b < 8; b++) {
						float ang = a * PI_F / 180.f;
						char w[2] = {(char)('a' + irand(26)), 0};
						for (int t = 0; t < 20 && firstLetterTaken(w[0]); t++) w[0] = (char)('a' + irand(26));
						newEnt(T_BULLET, e->x + 18 + cosf(ang) * 21, e->y + 48 + sinf(ang) * 21, w, ang, 1);
						a += inc;
					}
				}
				e->shootT = 7.f;
			}
		}
		if (e->x < -TY[e->type].sheetW || e->x > NW + 10 || e->y > NH + 10 || e->y < -TY[e->type].sheetH - 30) { killEnt(e, 1); continue; }
		if (pl1.alive && e->x < pl1.x + 8 && e->x + TY[e->type].w > pl1.x && e->y < pl1.y + 8 && e->y + TY[e->type].h > pl1.y) {
			explodeParts(e, 1);
			killEnt(e, 0);
			killPlayer();
		}
	}
}

static void updatePlasma(float dt) {
	for (int i = 0; i < MAXP; i++) {
		Plasma *p = &pl[i];
		if (!p->on) continue;
		Ent *e = &en[p->target];
		if (!e->on) { p->on = 0; continue; }
		float tx = e->x + TY[e->type].w * .5f, ty = e->y + TY[e->type].h * .5f;
		float dx = tx - p->x, dy = ty - p->y, d = sqrtf(dx * dx + dy * dy);
		if (d > p->dist || d < TY[e->type].h) { p->on = 0; hitEnt(e); continue; }
		p->dist = d;
		p->vx = dx / d * 800.f; p->vy = dy / d * 800.f;
		p->x += p->vx * dt; p->y += p->vy * dt;
	}
}

void game_update(float dt) {
	tnow += dt;
	menuT += dt;
	if (msgT > 0) msgT -= dt;
	shake *= powf(1.f / 1.1f, dt * 60.f);
	if (shake < .5f) shake = 0;
	shx = frand() * shake; shy = frand() * shake;

	if (menu != MENU_NONE) yScroll -= 100.f * dt;
	if (gameTrans) {
		gameTransT += dt;
		pl1.y = easeIO(gameTransT, 400, 242, 2.f);
		if (gameTransT >= 2.f) { gameTrans = 0; menu = MENU_NONE; pl1.y = 642; }
		return;
	}
	if (menu != MENU_NONE) {
		if (mode == MODE_OVER) {
			overT += dt;
			if (!overShown && overT > 1.f) { overShown = 1; menu = MENU_OVER; cur = 0; menuT = 0; }
		}
		if (menu != MENU_OVER) return;
	}

	/* particles & effects always run in game / game-over */
	for (int i = 0; i < MAXPT; i++) if (pt[i].on) {
		Part *p = &pt[i];
		p->t += dt; p->x += p->vx * dt; p->y += p->vy * dt; p->rot += .1f * dt * 60.f;
		if (p->t > .5f) p->on = 0;
	}
	for (int i = 0; i < 6; i++) if (boom[i].on) { boom[i].t += dt; if (boom[i].t > 1.f) boom[i].on = 0; }
	if (killFlash > 0) killFlash -= dt;
	if (pl1.flashT > 0) pl1.flashT -= dt;
	if (pl1.missT > 0) pl1.missT -= dt;
	if (multiT > 0) multiT -= dt;

	if (mode == MODE_OVER) {
		if (menu == MENU_NONE) { overT += dt; if (!overShown && overT > 1.f) { overShown = 1; menu = MENU_OVER; cur = 0; menuT = 0; } }
		updateEnts(dt); updatePlasma(dt);
		yScroll -= 100.f * dt; yScroll2 += dt * 10.f;
		return;
	}
	if (mode != MODE_GAME) return;

	/* angle smoothing */
	float ad = pl1.angle - pl1.targetAngle;
	if (fabsf(ad) < .02f) pl1.angle = pl1.targetAngle; else pl1.angle -= ad * dt * 10.f;
	idleT += dt;
	gameT += dt;

	/* wave spawning */
	if (!nSpawn) {
		int busy = 0;
		for (int i = 0; i < MAXE; i++) if (en[i].on) busy = 1;
		for (int i = 0; i < MAXP; i++) if (pl[i].on) busy = 1;
		if (!busy && !waveEnd) { waveEnd = 1; waveEndT = 2.f; }
		else if (waveEnd) { waveEndT -= dt; if (waveEndT <= 0) { waveEnd = 0; nextWave(); } }
	} else {
		spawnT += dt;
		if (spawnT > curSpawnWait) {
			spawnT = 0;
			Spawn s = spawnList[--nSpawn];
			newEnt(s.type, mapf(frand(), 0, 1, 10, NW - 10), -30.f, s.word[0] ? s.word : NULL, 0, 0);
			curSpawnWait = spawnWait;
		}
	}
	updateEnts(dt);
	updatePlasma(dt);

	if (emp.on) {
		emp.t += dt;
		float alpha = 1.f - emp.t, sc = sqrtf(1.7f * emp.t) * 200.f;
		(void)alpha;
		for (int i = 0; i < MAXE; i++) if (en[i].on) {
			float dx = en[i].x + TY[en[i].type].w * .5f - emp.x, dy = en[i].y + TY[en[i].type].h * .5f - emp.y;
			if (sqrtf(dx * dx + dy * dy) < sc) { explodeParts(&en[i], 1); killEnt(&en[i], 0); }
		}
		if (emp.t > 1.f) emp.on = 0;
	}

	float scroll = 100.f;
	if (waveEnd) {
		pl1.targetAngle = 0; idleT = 0;
		float s = sinf(waveEndT * .5f * PI_F);
		scroll = 100.f + s * s * 300.f;
	}
	yScroll2 += dt * scroll * .1f;
	yScroll -= dt * scroll;
}

/* ---------------- drawing: game ---------------- */
static void txtc(Font *f, float cx, float cy, const char *s, uint32_t c, float a, int align) {
	text(f, SX(cx), SY(cy) - text_h(f) * .5f, s, c, (int)(a * 255), 0, align);
}
static void txtt(Font *f, float x, float y, const char *s, uint32_t c, float a, int align, float sp) {
	text(f, SX(x), SY(y), s, c, (int)(a * 255), sp, align);
}

static void drawGrid(float alpha) {
	int a = (int)(alpha * 150);
	if (a <= 0) return;
	float cell = 31.f;
	float off = fmodf(yScroll, cell); if (off < 0) off += cell;
	for (float y = off - cell; y < NH + cell; y += cell) {
		int py = (int)(SY(y) + .5f);
		for (int x = FX; x < FX + FW; x++) blend(x, py, RGB(150, 225, 220), a);
	}
	for (float x = 0; x <= NW; x += cell) {
		int px = (int)(SX(x) + .5f);
		if (px < FX || px >= FX + FW) continue;
		for (int y = FY; y < FY + FH; y++) blend(px, y, RGB(150, 225, 220), a);
	}
}

static void drawLabel(Ent *e) {
	if (e->rem >= e->len) return;
	float w = text_w(fWord, e->word, .3f) / KS;
	float x = clampf(e->x - 6, w + 2, NW - 1), y = clampf(e->y + TY[e->type].h - 10, 2, NH - 19);
	fill_rect((int)(SX(x - w - 2) + .5f), (int)(SY(y + 4) + .5f), (int)((w + 8) * KS + .5f), (int)(24 * KS + .5f), RGB(0, 0, 0), 191);
	uint32_t col = e->targeted ? RGB(255, 132, 40) : RGB(255, 255, 255);
	txtc(fWord, x + 2, y + 16, e->word + e->rem, col, 1.f, AL_RIGHT);
}

static void drawReticle(Ent *e) {
	float d = mapf(e->retT, .5f, 0.f, 0.f, 1.f);
	float s = (1 - d) * (1 - d) * 4.f + 1.f;
	float cx = e->x + TY[e->type].w * .5f, cy = e->y + TY[e->type].h * .5f;
	float rot = d * PI_F, k = sqrtf(clampf(d, 0.f, 1.f));
	float r = 17.f * s;
	ring(SX(cx), SY(cy), r * KS, 1.4f, RGB(255, 170, 90), k * .9f);
	for (int i = 0; i < 4; i++) {
		float a = rot + i * PI_F * .5f;
		line_aa(SX(cx + cosf(a) * (r - 5)), SY(cy + sinf(a) * (r - 5)), SX(cx + cosf(a) * (r + 7)), SY(cy + sinf(a) * (r + 7)), 1.4f, RGB(255, 190, 120), (int)(k * 255), 1);
	}
}

static void drawParticles(void) {
	for (int i = 0; i < MAXPT; i++) if (pt[i].on) {
		Part *p = &pt[i];
		float a = 1.f - p->t / .5f;
		if (p->spark) {
			float l = 8 + 8 * (1 - a);
			for (int k = 0; k < 2; k++) {
				float ang = p->rot + k * PI_F * .5f;
				line_aa(SX(p->x - cosf(ang) * l), SY(p->y - sinf(ang) * l), SX(p->x + cosf(ang) * l), SY(p->y + sinf(ang) * l), 1.2f, RGB(255, 225, 150), (int)(a * 255), 1);
			}
			sglow(p->x, p->y, 8, RGB(255, 190, 90), a * .8f);
		} else {
			sglow(p->x, p->y, 10 + 12 * (1 - a), RGB(255, 120, 40), a * .85f);
			sglow(p->x, p->y, 5, RGB(255, 240, 200), a * .7f);
		}
	}
}

static void drawBooms(void) {
	for (int i = 0; i < 6; i++) if (boom[i].on) {
		Boom *b = &boom[i];
		float alpha = 1.f - b->t, sc = 10.f * b->t;
		float rad = clampf(sc * 64.f, 4.f, 420.f);
		sglow(b->x, b->y, rad, RGB(255, 140, 50), alpha * .9f);
		sglow(b->x, b->y, rad * .45f, RGB(255, 240, 210), alpha * .9f);
		if (b->glitch && (b->t < .35f)) {
			for (int k = 0; k < 4; k++) {
				int gy = FY + irand(FH - 6);
				fill_rect(FX, gy, FW, 1 + irand(3), RGB(60, 150, 255), 70 + irand(100));
			}
		}
	}
}

static void drawEMP(void) {
	if (!emp.on) return;
	float alpha = 1.f - emp.t, sc = sqrtf(1.7f * emp.t);
	float r = 128.f * 2.f * sc;
	ring(SX(emp.x), SY(emp.y), r * KS, 7.f, RGB(140, 200, 255), alpha);
	ring(SX(emp.x), SY(emp.y), r * KS * .8f, 14.f, RGB(70, 120, 255), alpha * .35f);
	sglow(emp.x, emp.y, r * .85f, RGB(60, 100, 220), alpha * .3f);
}

static void hexagon(float cx, float cy, float r, uint32_t c, int a) {
	float p[12];
	for (int i = 0; i < 6; i++) { float t = PI_F / 6.f + i * PI_F / 3.f - PI_F / 2.f + PI_F / 6.f; p[i * 2] = cx + cosf(t - PI_F / 6.f) * r; p[i * 2 + 1] = cy + sinf(t - PI_F / 6.f) * r; }
	int n[1] = {6};
	poly_fill(p, n, 1, c, a, 0, NULL, NULL);
}

static void drawHUD(void) {
	/* pause button (faint, top-left) */
	fill_rect((int)SX(22), (int)SY(22), 3, (int)(15 * KS + 1), RGB(255, 255, 255), 34);
	fill_rect((int)SX(34), (int)SY(22), 3, (int)(15 * KS + 1), RGB(255, 255, 255), 34);
	/* streak bar along the bottom edge */
	if (streak > 0) {
		float ml = 1.f - 75.f / (75.f + streak * 2.f);
		int w = (int)(NW * ml * KS);
		fill_rect(FX, FY + FH - 2, w, 2, RGB(80, 170, 235), 230);
		if (multiT > 0 && multiplier > 1) {
			float a = clampf(mapf(multiT, 2.f, 0.f, 2.f, 0.f), 0.f, 1.f);
			char b[16]; snprintf(b, sizeof b, "x%d", multiplier);
			int bx = FX + w - 22, by = FY + FH - 22;
			fill_rect(bx, by, 22, 13, RGB(30, 80, 140), (int)(a * 200));
			text(fSmall, bx + 11, by - 1, b, RGB(210, 240, 255), (int)(a * 255), 0, AL_CENTER);
		}
	}
	/* EMP badge */
	float bx = SX(448), by = SY(687);
	int a = emps ? 255 : 178;
	hexagon(bx, by, 12.5f, RGB(246, 176, 26), a);
	hexagon(bx, by, 10.5f, RGB(255, 200, 60), a);
	char b[16]; snprintf(b, sizeof b, "%d", emps);
	text(fTitle, bx, by - text_h(fTitle) * .5f, b, RGB(120, 70, 10), a, 0, AL_CENTER);
}

static void drawWaveClear(void) {
	if (!waveEnd) return;
	float d = waveEndT;
	float a = d > 1.7f ? mapf(d, 2.f, 1.7f, 0.f, 1.f) : d < 1.f ? mapf(d, 1.f, 0.f, 1.f, 0.f) : 1.f;
	float ys = 276 + (d < 1.f ? mapf(cosf(1 - d), 1.f, 0.f, 0.f, 250.f) : 0);
	char t[40], z[16];
	zfill(z, 3, wave);
	snprintf(t, sizeof t, "WAVE %s CLEAR", z);
	txtt(fTitle, 32, ys, t, RGB(110, 200, 245), a, AL_LEFT, 0);
	fill_rect((int)SX(32), (int)SY(ys + 48), (int)(276 * KS), 1, RGB(90, 170, 235), (int)(a * 200));
	zfill(z, 6, score);
	snprintf(t, sizeof t, "SCORE: %s", z);
	txtt(fSmall, 32, ys * 1.2f + 10, t, RGB(255, 255, 255), a, AL_LEFT, 0);
}

static void drawGame(void) {
	float d = killFlash > 0 ? killFlash + .1f : .1f;
	blitField(gbg, (int)(yScroll2 * KS));
	drawGrid(d);
	gfx_clip(FX, FY, FX + FW, FY + FH);
	for (int i = 0; i < MAXE; i++) if (en[i].on) { drawEnemySprite(&en[i]); if (en[i].retT > 0 && en[i].targeted) drawReticle(&en[i]); }
	for (int i = 0; i < MAXP; i++) if (pl[i].on) {
		Plasma *p = &pl[i];
		float l = sqrtf(p->vx * p->vx + p->vy * p->vy) + 1.f;
		line_aa(SX(p->x), SY(p->y), SX(p->x - p->vx / l * 26), SY(p->y - p->vy / l * 26), 2.f, RGB(90, 210, 255), 200, 1);
		sglow(p->x, p->y, 14, RGB(120, 230, 255), .9f);
		sglow(p->x, p->y, 6, RGB(255, 255, 255), .9f);
	}
	if (pl1.alive) drawShip(pl1.x + 4, pl1.y + 4, pl1.angle, 0, pl1.flashT > 0 ? pl1.flashT / .12f : 0, pl1.missT);
	drawParticles();
	drawEMP();
	drawBooms();
	for (int i = 0; i < MAXE; i++) if (en[i].on) drawLabel(&en[i]);
	if (mode == MODE_GAME && gameTrans == 0) {
		drawWaveClear();
		if (idleT > 8.f && menu == MENU_NONE) {
			float aa = clampf(mapf(idleT, 8.f, 9.f, 0.f, 1.f), 0.f, 1.f);
			float al = (sinf(idleT * 4.f) * .25f + .75f) * aa;
			txtc(fSmall, 240, 553, "Type the words to shoot!", RGB(255, 255, 255), al, AL_CENTER);
			txtc(fSmall, 240, 577, "ENTER for EMP", RGB(255, 255, 255), al, AL_CENTER);
		}
		drawHUD();
	}
	gfx_noclip();
}

/* ---------------- drawing: title & menus ---------------- */
static uint32_t logoGrad(float y, void *u) {
	const float *g = u;
	float t = clampf((y - g[0]) / (g[1] - g[0]), 0.f, 1.f);
	int v = (int)(252 - 34 * t);
	return RGB(v, v, v + 1);
}
static void drawLogo(float oy) {
	static const float Z[] = {0,0, 46,0, 46,24, 20,90, 46,90, 46,114, 0,114, 0,90, 26,24, 0,24};
	static const float T[] = {0,0, 46,0, 46,22, 34.5f,22, 34.5f,114, 11.5f,114, 11.5f,22, 0,22};
	static const float Y[] = {0,0, 17,0, 25,44, 33,0, 50,0, 37,66, 37,114, 13,114, 13,66};
	static const float P[] = {0,0, 30,0, 39,3, 44,11, 44,48, 39,59, 30,66, 18,66, 18,114, 0,114,
	                          18,22, 26,22, 29,25, 29,43, 26,46, 18,46};
	static const float E[] = {0,0, 40,0, 40,22, 18,22, 18,45, 36,45, 36,66, 18,66, 18,92, 40,92, 40,114, 0,114};
	static const struct { const float *p; int n, n2; float w; } L[5] = {{Z, 10, 0, 46}, {T, 8, 0, 46}, {Y, 9, 0, 50}, {P, 10, 6, 44}, {E, 12, 0, 40}};
	const float fx = 383.f / 256.f, fy = 171.f / 114.f;
	float x = 50.f, g[2] = {SY(113.f + oy), SY(284.f + oy)};
	for (int i = 0; i < 5; i++) {
		float pts[64]; int cnt[2] = {L[i].n, L[i].n2}, tot = L[i].n + L[i].n2;
		for (int k = 0; k < tot; k++) { pts[k * 2] = SX(x + L[i].p[k * 2] * fx); pts[k * 2 + 1] = SY(113.f + oy + L[i].p[k * 2 + 1] * fy); }
		poly_fill(pts, cnt, L[i].n2 ? 2 : 1, 0, 255, 0, logoGrad, g);
		x += (L[i].w + 7.5f) * fx;
	}
}

static void drawTitleScene(float tr, float shipY) {
	blitField(nebula, (int)(menuT * 10.f * KS * .75f));
	gfx_clip(FX, FY, FX + FW, FY + FH);
	float up = easeIO(tr, 0, 1000, 1) * .75f, up2 = easeIO(tr, 0, 2800, 1) * .75f;
	/* PHOBOSLAB wordmark */
	{
		const char *s = "PHOBOSLAB";
		float base = text_w(fMenuS, s, 0), sp = (155.f * KS - base) / 8.f;
		text(fMenuS, SX(240), SY(67.f - up2) - text_h(fMenuS) * .5f, s, RGB(255, 255, 255), 255, sp, AL_CENTER);
	}
	drawLogo(-up);
	/* exhaust beam under the ship */
	float sx = SX(239.f), sy = SY(shipY + 6.f);
	for (int i = 0; i < 95; i++) {
		float t = i / 95.f;
		blend((int)sx, (int)(sy + 8 + i), RGB(70, 100, 190), (int)((1.f - t) * 70));
		blend((int)sx + 1, (int)(sy + 8 + i), RGB(70, 100, 190), (int)((1.f - t) * 35));
	}
	drawShip(239.f, shipY + 6.f, 0, 1, 0, 0);
	gfx_noclip();
}

static void menuItem(const char *s, float cx, float cy, int sel, float alpha) {
	uint32_t c = sel ? RGB(232, 108, 28) : RGB(255, 255, 255);
	txtc(fMenu, cx, cy, s, c, alpha, AL_CENTER);
}

static void bar(float x, float y, float w, int a) { fill_rect((int)SX(x), (int)SY(y), (int)(w * KS), 1, RGB(80, 160, 230), a); }

static void drawChart(float x, float y, float w, float h) {
	txtt(fTiny, x, y - 16, "RECENT GAMES", RGB(255, 255, 255), .5f, AL_LEFT, 0);
	int n = sv.nHist;
	int mx = 1;
	for (int i = 0; i < n; i++) if (sv.hist[i] > mx) mx = sv.hist[i];
	float bw = w / HIST;
	for (int i = 0; i < n; i++) {
		float bh = h * sv.hist[i] / mx;
		fill_rect((int)SX(x + i * bw + 2), (int)SY(y + h - bh), (int)((bw - 4) * KS), (int)(bh * KS + 1), RGB(70, 140, 215), i == n - 1 ? 255 : 150);
	}
	fill_rect((int)SX(x), (int)SY(y + h), (int)(w * KS), 1, RGB(90, 170, 235), 150);
}

static void drawBackItem(float cy, int sel) { menuItem("back to title", 240, cy, sel, 1.f); }

static void drawStatsScreen(const char *l1, int v1, int wave_, const char *l2, const char *v2s, const char *l3, const char *v3s, const char *l4, int v4, int showPB) {
	char z[16], t[40];
	txtt(fTiny, 24, 25, l1, RGB(255, 255, 255), .5f, AL_LEFT, 0);
	txtt(fTiny, 252, 25, l2, RGB(255, 255, 255), .5f, AL_LEFT, 0);
	zfill(z, 6, v1); txtt(fTitle, 24, 50, z, RGB(110, 200, 245), 1.f, AL_LEFT, 0);
	(void)v2s;
	zfill(z, 3, wave_); snprintf(t, sizeof t, "WAVE %s", z); txtt(fTitle, 252, 50, t, RGB(110, 200, 245), 1.f, AL_LEFT, 0);
	bar(24, 95, 432, 200);
	txtt(fTiny, 24, 165, l3, RGB(255, 255, 255), .5f, AL_LEFT, 0);
	txtt(fTiny, 252, 165, l4, RGB(255, 255, 255), .5f, AL_LEFT, 0);
	txtt(fTitle, 24, 190, v3s, RGB(110, 200, 245), 1.f, AL_LEFT, 0);
	snprintf(t, sizeof t, "%d", v4); txtt(fTitle, 252, 190, t, RGB(110, 200, 245), 1.f, AL_LEFT, 0);
	bar(24, 235, 432, 200);
	if (showPB) {
		disc(SX(24 + 14), SY(275 + 14), 9.f * KS * 1.3f, RGB(246, 176, 26), 255);
		disc(SX(24 + 14), SY(275 + 14), 6.f * KS * 1.3f, RGB(255, 210, 80), 255);
		txtt(fSmall, 60, 281, "NEW PERSONAL BEST", RGB(255, 255, 255), 1.f, AL_LEFT, 0);
	}
	drawChart(24, 355, 432, 150);
}

static void drawMenu(void) {
	char t[48];
	switch (menu) {
	case MENU_SETTINGS: {
		gfx_clip(FX, FY, FX + FW, FY + FH);
		fill_rect(FX, FY, FW, FH, RGB(0, 0, 0), 204);
		txtt(fMenu, 240, 51, mode == MODE_GAME ? "GAME PAUSED" : "SETTINGS", RGB(255, 255, 255), .5f, AL_CENTER, 0);
		txtt(fTiny, 240, 160, "SOUND", RGB(255, 255, 255), .5f, AL_CENTER, 0);
		txtt(fTiny, 240, 260, "MUSIC", RGB(255, 255, 255), .5f, AL_CENTER, 0);
		snprintf(t, sizeof t, "%d%%", (int)(volSfx * 100 + .5f));
		menuItem(t, 240, 207, cur == 0, 1.f);
		snprintf(t, sizeof t, "%d%%", (int)(volMusic * 100 + .5f));
		menuItem(t, 240, 307, cur == 1, 1.f);
		txtc(fMenu, 150, 207, "-", RGB(255, 255, 255), .5f, AL_CENTER); txtc(fMenu, 330, 207, "+", RGB(255, 255, 255), .5f, AL_CENTER);
		txtc(fMenu, 150, 307, "-", RGB(255, 255, 255), .5f, AL_CENTER); txtc(fMenu, 330, 307, "+", RGB(255, 255, 255), .5f, AL_CENTER);
		if (mode == MODE_GAME) { menuItem("resume", 240, 517, cur == 2, 1.f); drawBackItem(560, cur == 3); }
		else drawBackItem(517, cur == 2);
		gfx_noclip();
		break; }
	case MENU_OVER: {
		gfx_clip(FX, FY, FX + FW, FY + FH);
		int acc = hits ? hits * 1000 / (hits + misses) : 0;
		snprintf(t, sizeof t, "%d.%d%%", acc / 10, acc % 10);
		drawStatsScreen("FINAL SCORE", score, wave, "YOU REACHED", "", "ACCURACY", t, "LONGEST STREAK", longestStreak, isBest);
		drawBackItem(609, 1);
		gfx_noclip();
		break; }
	case MENU_STATS: {
		gfx_clip(FX, FY, FX + FW, FY + FH);
		fill_rect(FX, FY, FW, FH, RGB(0, 0, 0), 150);
		int acc = sv.games ? sv.accSum / sv.games : 0;
		snprintf(t, sizeof t, "%d.%d%%", acc / 10, acc % 10);
		drawStatsScreen("PERSONAL BEST", sv.best, sv.bestWave, "BEST WAVE", "", "AVERAGE ACCURACY", t, "LONGEST STREAK", sv.bestStreak, 0);
		snprintf(t, sizeof t, "%d GAMES PLAYED", sv.games);
		txtt(fTiny, 24, 520, t, RGB(255, 255, 255), .5f, AL_LEFT, 0);
		drawBackItem(609, 1);
		gfx_noclip();
		break; }
	case MENU_ABOUT: {
		blitField(nebula, (int)(menuT * 10.f * KS));
		gfx_clip(FX, FY, FX + FW, FY + FH);
		txtc(fMenu, 240, 140, "ZTYPE", RGB(255, 255, 255), 1.f, AL_CENTER);
		txtc(fSmall, 240, 215, "ORIGINAL GAME BY PHOBOSLAB", RGB(255, 255, 255), .8f, AL_CENTER);
		txtc(fSmall, 240, 245, "PHOBOSLAB.ORG", RGB(255, 255, 255), .5f, AL_CENTER);
		txtc(fSmall, 240, 300, "SOUNDTRACK BY NOFATENET", RGB(255, 255, 255), .8f, AL_CENTER);
		txtc(fSmall, 240, 360, "WII VERSION BY QUATRIC", RGB(255, 255, 255), .8f, AL_CENTER);
		txtc(fSmall, 240, 440, "PUT YOUR OWN TEXT IN", RGB(255, 255, 255), .5f, AL_CENTER);
		txtc(fSmall, 240, 460, "SD:/APPS/TYPESHOT/TEXT.TXT", RGB(255, 255, 255), .5f, AL_CENTER);
		drawBackItem(609, 1);
		gfx_noclip();
		break; }
	default: break;
	}
}

static void drawTitleMenu(float tr) {
	gfx_clip(FX, FY, FX + FW, FY + FH);
	float y0 = easeIO(tr, 690.f, 1600.f, 1.f) * .75f + 19.5f;
	float y1 = easeIO(tr, 760.f, 1900.f, 1.f) * .75f + 19.5f;
	float y2 = easeIO(tr, 820.f, 2100.f, 1.f) * .75f + 19.5f;
	float y3 = easeIO(tr, 880.f, 2200.f, 1.f) * .75f + 19.5f;
	menuItem("new game", 240, y0, cur == 0, .9f);
	menuItem("settings", 240, y1, cur == 1, .4f);
	menuItem("my stats", 240, y2, cur == 2, .4f);
	menuItem("load your own text", 240, y3, cur == 3, 1.f);
	/* info icon */
	float ix = SX(432), iy = SY(672), ia = clampf(.4f - tr, 0.f, 1.f);
	ring(ix, iy, 9.f, 1.f, RGB(255, 255, 255), ia * .9f);
	fill_rect((int)ix - 1, (int)iy - 1, 2, 6, RGB(255, 255, 255), (int)(ia * 220));
	fill_rect((int)ix - 1, (int)iy - 5, 2, 2, RGB(255, 255, 255), (int)(ia * 220));
	if (msgT > 0) txtc(fSmall, 240, 30, msg, RGB(255, 255, 255), clampf(msgT, 0.f, 1.f) * .8f, AL_CENTER);
	gfx_noclip();
}

void game_draw(void) {
	memcpy(fb, bgPage, sizeof fb);
	drawPageText();
	if (!kbOn) text(fFoot, 8, 8, "NO USB KEYBOARD DETECTED", RGB(200, 90, 80), 200, 0, AL_LEFT);

	if (mode == MODE_TITLE && menu == MENU_TITLE) {
		drawTitleScene(0, 400.f);
		drawTitleMenu(0);
	} else if (mode == MODE_TITLE && menu == MENU_SETTINGS) {
		fill_rect(FX, FY, FW, FH, RGB(0, 0, 0), 255);
		drawMenu();
	} else if (mode == MODE_TITLE && (menu == MENU_STATS)) {
		blitField(nebula, (int)(menuT * 10.f * KS));
		drawMenu();
	} else if (mode == MODE_TITLE && menu == MENU_ABOUT) {
		drawMenu();
	} else {
		drawGame();
		if (gameTrans) {
			float tr = clampf(gameTransT / 2.f, 0.f, 1.f);
			for (int y = 0; y < FH; y++) memcpy(tmpField[y], &fb[FY + y][FX], FW * 4);
			drawTitleScene(tr, pl1.y);
			drawTitleMenu(tr);
			float a = clampf(1.f - tr, 0.f, 1.f) * (1.f - .35f * frand() * sinf(tr * PI_F));
			int ia = (int)(a * 256);
			for (int y = 0; y < FH; y++) for (int x = 0; x < FW; x++) {
				uint32_t s = fb[FY + y][FX + x], d = tmpField[y][x];
				int r = CR(d) + (((int)CR(s) - (int)CR(d)) * ia >> 8), g = CG(d) + (((int)CG(s) - (int)CG(d)) * ia >> 8), b = CB(d) + (((int)CB(s) - (int)CB(d)) * ia >> 8);
				fb[FY + y][FX + x] = RGB(clampi(r, 0, 255), clampi(g, 0, 255), clampi(b, 0, 255));
			}
		} else drawMenu();
	}
}

/* ---------------- menu navigation ---------------- */
static void menuNav(int k) {
	if (gameTrans) return;
	switch (menu) {
	case MENU_TITLE:
		if (k == K_UP && cur > 0) { cur--; sound_play(SFX_CLICK); }
		else if (k == K_DOWN && cur < 3) { cur++; sound_play(SFX_CLICK); }
		else if (k == K_ABOUT) { menu = MENU_ABOUT; menuT = 0; }
		else if (k == K_OK) {
			sound_play(SFX_CLICK);
			if (cur == 0) setGame();
			else if (cur == 1) { menu = MENU_SETTINGS; cur = 0; }
			else if (cur == 2) { menu = MENU_STATS; cur = 0; menuT = 0; }
			else {
				int n = loadText();
				if (n >= 8) { customOn = 1; snprintf(msg, sizeof msg, "TEXT LOADED - %d WORDS", n); }
				else if (n < 0) { customOn = 0; snprintf(msg, sizeof msg, "NO TEXT.TXT IN SD:/APPS/TYPESHOT"); }
				else { customOn = 0; snprintf(msg, sizeof msg, "TEXT NEEDS AT LEAST 8 WORDS"); }
				msgT = 3.f;
			}
		}
		break;
	case MENU_SETTINGS: {
		int n = mode == MODE_GAME ? 4 : 3;
		if (k == K_MENU && mode == MODE_GAME) { menu = MENU_NONE; break; }
		if (k == K_MENU) { menu = MENU_TITLE; cur = 1; break; }
		if (k == K_UP && cur > 0) { cur--; sound_play(SFX_CLICK); }
		else if (k == K_DOWN && cur < n - 1) { cur++; sound_play(SFX_CLICK); }
		else if ((k == K_LEFT || k == K_RIGHT) && cur < 2) {
			float d = k == K_LEFT ? -.1f : .1f;
			if (cur == 0) { volSfx = clampf(volSfx + d, 0.f, 1.f); sound_set_sfx_volume(volSfx); sound_play(SFX_CLICK); }
			else { volMusic = clampf(volMusic + d, 0.f, 1.f); sound_set_music_volume(volMusic); }
		} else if (k == K_OK) {
			if (mode == MODE_GAME && cur == 2) menu = MENU_NONE;
			else if (cur >= 2) { setTitle(); }
		}
		break; }
	case MENU_OVER:
		if (k == K_OK && overT > 2.5f) { sound_play(SFX_CLICK); setTitle(); }
		break;
	case MENU_STATS:
	case MENU_ABOUT:
		if (k == K_OK || k == K_MENU) { cur = menu == MENU_STATS ? 2 : 0; menu = MENU_TITLE; sound_play(SFX_CLICK); }
		break;
	default: break;
	}
}

void game_init(int sd) {
	sdOK = sd;
	svLoad();
	bucketWords();
	makeArt();
	setTitle();
	sound_set_sfx_volume(volSfx);
	sound_set_music_volume(volMusic);
}
