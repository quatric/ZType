/* TypeShot Wii - a typing shooter for Wii with USB keyboard support.
 * Original game inspired by the "type to shoot" genre. Software renderer. */
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <wiikeyboard/keyboard.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sound.h"
#include <math.h>

#define W 320
#define H 240

static u32 fb[H][W];
static u32 yuvrow[W];
static void *xfb[2];
static int fbi = 0;
static GXRModeObj *rmode;

/* ---------------- font (5x7) ---------------- */
static const char FCH[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:-.%/!>?'";
static const u8 FD[][7] = {
 {0,0,0,0,0,0,0},
 {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
 {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
 {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
 {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
 {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
 {0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
 {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x11,0x19,0x15,0x13,0x11,0x11},
 {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
 {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
 {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
 {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
 {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
 {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
 {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
 {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
 {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
 {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
 {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
 {0x00,0x04,0x00,0x00,0x04,0x00,0x00},{0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
 {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},{0x18,0x19,0x02,0x04,0x08,0x13,0x03},
 {0x01,0x02,0x02,0x04,0x08,0x08,0x10},{0x04,0x04,0x04,0x04,0x04,0x00,0x04},
 {0x10,0x08,0x04,0x02,0x04,0x08,0x10},{0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
 {0x04,0x04,0x08,0x00,0x00,0x00,0x00},
};

#define RGB(r,g,b) (((r)<<16)|((g)<<8)|(b))

static inline void px(int x, int y, u32 c) {
	if ((unsigned)x < W && (unsigned)y < H) fb[y][x] = c;
}
static void rect(int x, int y, int w, int h, u32 c) {
	for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) px(i, j, c);
}
static void line(int x0, int y0, int x1, int y1, u32 c) {
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
	for (;;) {
		px(x0, y0, c);
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}
static void glyph(int x, int y, char ch, int s, u32 c) {
	if (ch >= 'a' && ch <= 'z') ch -= 32;
	const char *p = strchr(FCH, ch);
	if (!p || !ch) return;
	const u8 *g = FD[p - FCH];
	for (int r = 0; r < 7; r++)
		for (int b = 0; b < 5; b++)
			if (g[r] & (0x10 >> b)) rect(x + b * s, y + r * s, s, s, c);
}
static void text(int x, int y, const char *t, int s, u32 c) {
	for (; *t; t++, x += 6 * s) glyph(x, y, *t, s, c);
}
static void textc(int cx, int y, const char *t, int s, u32 c) {
	text(cx - (int)strlen(t) * 3 * s, y, t, s, c);
}

/* ---------------- words ---------------- */
static const char *WORDS[] = {
 "go","up","ion","orb","ray","gas","jet","sun","fly","hit","arc","fox","zap","hex","bit",
 "star","moon","nova","dust","beam","warp","void","core","comet","laser","orbit","pulse","probe",
 "flare","rocket","shield","plasma","galaxy","engine","thrust","cosmos","cannon","fusion",
 "meteor","photon","quasar","vector","signal","cluster","reactor","gravity","phantom","station",
 "capsule","eclipse","nebulae","asteroid","starship","universe","blaster","hyperdrive",
 "telescope","satellite","supernova","spacewalk","wormhole","antimatter","constellation",
 "interstellar","extraterrestrial", 0
};
static int nwords;

/* ---------------- game state ---------------- */
#define MAXE 24
#define MAXB 48
#define MAXP 256

typedef struct { int on; char w[20]; int len, typed, inflight, dying; float x, y, vy, ph; int kind; } Enemy;
typedef struct { int on; float x, y; int e; } Bullet;
typedef struct { int on; float x, y, vx, vy, life; u32 c; } Part;
typedef struct { float x, y, v; u32 c; } Star;

static Enemy en[MAXE];
static Bullet bu[MAXB];
static Part pa[MAXP];
static Star st[90];

enum { S_TITLE, S_PLAY, S_PAUSE, S_OVER };
static int state = S_TITLE;
static int level, spawned, toSpawn, target, score, combo, bestCombo, shield;
static int keysHit, keysMiss, kills, frames, spawnT, bannerT;
static int hiscore, shake, kbOn;
static unsigned rng = 12345;
static int rnd(int n) { rng = rng * 1103515245 + 12345; return (int)((rng >> 16) % (unsigned)n); }

static void burst(float x, float y, int n, u32 c, float sp) {
	for (int i = 0; i < n; i++)
		for (int k = 0; k < MAXP; k++) if (!pa[k].on) {
			float a = rnd(628) / 100.f, v = (0.3f + rnd(100) / 100.f) * sp;
			pa[k] = (Part){1, x, y, cosf(a) * v, sinf(a) * v, 20 + rnd(25), c};
			break;
		}
}

static void startLevel(int l) {
	level = l; spawned = 0; toSpawn = 5 + l * 2; spawnT = 30; bannerT = 100; target = -1;
	memset(bu, 0, sizeof bu);
}
static void newGame(void) {
	memset(en, 0, sizeof en); memset(bu, 0, sizeof bu); memset(pa, 0, sizeof pa);
	score = combo = bestCombo = keysHit = keysMiss = kills = frames = 0; shield = 100;
	startLevel(1); state = S_PLAY;
}
static int usedFirst(char c) {
	for (int i = 0; i < MAXE; i++) if (en[i].on && !en[i].dying && en[i].w[0] == c) return 1;
	return 0;
}
static void spawnEnemy(void) {
	for (int i = 0; i < MAXE; i++) if (!en[i].on) {
		int minl = 2 + level / 2, maxl = 4 + level;
		if (maxl > 16) maxl = 16;
		const char *w; int tries = 0;
		do {
			w = WORDS[rnd(nwords)];
			tries++;
		} while ((((int)strlen(w) < minl || (int)strlen(w) > maxl) || (usedFirst(w[0]) && tries < 60)) && tries < 200);
		Enemy *e = &en[i];
		memset(e, 0, sizeof *e);
		e->on = 1; strcpy(e->w, w); e->len = strlen(w);
		int ww = e->len * 12;
		e->x = 8 + ww / 2 + rnd(W - 16 - ww); e->y = -20;
		e->vy = 0.22f + level * 0.035f + rnd(10) / 100.f; e->ph = rnd(628) / 100.f; e->kind = rnd(3);
		spawned++;
		return;
	}
}
static void killEnemy(int i) {
	sound_play(SFX_EXPLODE);
	burst(en[i].x, en[i].y, 30, RGB(255, 200, 80), 1.8f);
	burst(en[i].x, en[i].y, 14, RGB(255, 255, 255), 1.2f);
	score += en[i].len * 20 * (1 + combo / 10); kills++;
	if (target == i) target = -1;
	en[i].on = 0; shake = 4;
}
static void shoot(int e) {
	for (int k = 0; k < MAXB; k++) if (!bu[k].on) {
		bu[k] = (Bullet){1, W / 2, 205, e}; en[e].inflight++; sound_play(SFX_SHOOT); return;
	}
}
static void typeKey(char c) {
	if (c >= 'A' && c <= 'Z') c += 32;
	if (c < 'a' || c > 'z') return;
	if (target < 0) {
		float by = -1e9; int bi = -1;
		for (int i = 0; i < MAXE; i++)
			if (en[i].on && !en[i].dying && en[i].typed == 0 && en[i].w[0] == c && en[i].y > by && en[i].y > -10) { by = en[i].y; bi = i; }
		if (bi >= 0) target = bi;
	}
	if (target >= 0 && en[target].w[en[target].typed] == c) {
		Enemy *e = &en[target];
		e->typed++; shoot(target); keysHit++; combo++;
		if (combo > bestCombo) bestCombo = combo;
		score += 10 * (1 + combo / 10);
		if (e->typed == e->len) { e->dying = 1; target = -1; }
	} else { keysMiss++; combo = 0; sound_play(SFX_MISS); burst(W / 2, 210, 6, RGB(255, 60, 60), 1.f); }
}

static void update(void) {
	frames++;
	if (shake) shake--;
	for (int i = 0; i < 90; i++) { st[i].y += st[i].v; if (st[i].y > H) { st[i].y = 0; st[i].x = rnd(W); } }
	for (int k = 0; k < MAXP; k++) if (pa[k].on) {
		pa[k].x += pa[k].vx; pa[k].y += pa[k].vy; pa[k].vx *= .96f; pa[k].vy *= .96f;
		if (--pa[k].life <= 0) pa[k].on = 0;
	}
	if (state != S_PLAY) return;
	if (bannerT > 0) bannerT--;
	if (spawned < toSpawn && --spawnT <= 0) { spawnEnemy(); spawnT = 70 - level * 3; if (spawnT < 25) spawnT = 25; }
	int alive = 0;
	for (int i = 0; i < MAXE; i++) if (en[i].on) {
		Enemy *e = &en[i]; alive++;
		e->y += e->vy; e->ph += 0.05f; e->x += sinf(e->ph) * 0.25f;
		int hw = e->len * 6 + 4;
		if (e->x < hw) e->x = hw;
		if (e->x > W - hw) e->x = W - hw;
		if (e->y > 195) {
			shield -= 20; combo = 0; shake = 10; sound_play(SFX_BREACH);
			burst(e->x, e->y, 30, RGB(255, 80, 40), 2.f);
			if (target == i) target = -1;
			e->on = 0;
			for (int k = 0; k < MAXB; k++) if (bu[k].on && bu[k].e == i) bu[k].on = 0;
			if (shield <= 0) { shield = 0; state = S_OVER; sound_play(SFX_OVER); if (score > hiscore) hiscore = score; }
		}
	}
	for (int k = 0; k < MAXB; k++) if (bu[k].on) {
		Bullet *b = &bu[k];
		if (!en[b->e].on) { b->on = 0; continue; }
		float dx = en[b->e].x - b->x, dy = en[b->e].y - b->y, d = sqrtf(dx * dx + dy * dy);
		if (d < 8) {
			b->on = 0; en[b->e].inflight--;
			burst(b->x, b->y, 5, RGB(120, 255, 200), 1.f);
			if (en[b->e].dying && en[b->e].inflight <= 0) killEnemy(b->e);
		} else { b->x += dx / d * 7; b->y += dy / d * 7; }
	}
	if (spawned >= toSpawn && alive == 0 && state == S_PLAY) {
		score += 100 * level; startLevel(level + 1); sound_play(SFX_LEVEL);
		if (shield < 100) { shield += 10; if (shield > 100) shield = 100; }
	}
}

static void drawShip(int cx, int cy) {
	u32 a = RGB(90, 220, 255), b = RGB(255, 255, 255);
	line(cx, cy - 12, cx - 10, cy + 8, a); line(cx, cy - 12, cx + 10, cy + 8, a);
	line(cx - 10, cy + 8, cx, cy + 3, a); line(cx + 10, cy + 8, cx, cy + 3, a);
	px(cx, cy - 6, b); px(cx, cy - 5, b); rect(cx - 1, cy + 4, 3, 2 + (frames / 3) % 3, RGB(255, 160, 40));
}
static void drawEnemy(int i) {
	Enemy *e = &en[i]; int x = (int)e->x, y = (int)e->y;
	u32 c = e->kind == 0 ? RGB(255, 90, 120) : e->kind == 1 ? RGB(255, 200, 70) : RGB(190, 110, 255);
	line(x - 8, y - 12, x + 8, y - 12, c); line(x - 8, y - 12, x, y - 4, c); line(x + 8, y - 12, x, y - 4, c);
	line(x - 8, y - 12, x - 12, y - 16, c); line(x + 8, y - 12, x + 12, y - 16, c);
	px(x - 3, y - 11, c); px(x + 3, y - 11, c);
	int w = e->len * 12, tx = x - w / 2;
	rect(tx - 2, y - 2, w + 3, 16, i == target ? RGB(0, 60, 40) : RGB(10, 10, 30));
	for (int k = 0; k < e->len; k++) {
		u32 gc = k < e->typed ? RGB(80, 255, 160) : RGB(235, 235, 255);
		glyph(tx + k * 12, y + 1, e->w[k], 2, gc);
	}
}

static void render(void) {
	rect(0, 0, W, H, RGB(4, 4, 18));
	for (int i = 0; i < 90; i++) { int v = 60 + (int)(st[i].v * 90); px((int)st[i].x, (int)st[i].y, RGB(v, v, v + 20)); }
	for (int i = 0; i < MAXE; i++) if (en[i].on) drawEnemy(i);
	if (state != S_TITLE) {
		if (target >= 0 && en[target].on) {
			line(W / 2, 195, (int)en[target].x, (int)en[target].y, RGB(40, 120, 90));
			int x = (int)en[target].x, y = (int)en[target].y, w = en[target].len * 6 + 5;
			u32 r = RGB(80, 255, 160);
			line(x - w, y - 4, x - w, y + 15, r); line(x + w, y - 4, x + w, y + 15, r);
		}
		for (int k = 0; k < MAXB; k++) if (bu[k].on) { rect((int)bu[k].x - 1, (int)bu[k].y - 1, 3, 3, RGB(180, 255, 220)); }
		drawShip(W / 2, 212);
	}
	for (int k = 0; k < MAXP; k++) if (pa[k].on) { int v = pa[k].life > 20 ? 20 : (int)pa[k].life;
		u32 c = pa[k].c; int r = ((c >> 16) & 255) * v / 20, g = ((c >> 8) & 255) * v / 20, b = (c & 255) * v / 20;
		px((int)pa[k].x, (int)pa[k].y, RGB(r, g, b)); }
	char buf[64];
	if (state != S_TITLE) {
		snprintf(buf, sizeof buf, "SCORE %d", score); text(4, 3, buf, 1, RGB(200, 200, 255));
		snprintf(buf, sizeof buf, "LEVEL %d", level); textc(W / 2, 3, buf, 1, RGB(200, 200, 255));
		if (combo > 1) { snprintf(buf, sizeof buf, "COMBO %d", combo); text(W - 70, 3, buf, 1, RGB(255, 220, 90)); }
		rect(4, H - 9, 102, 6, RGB(40, 40, 60)); rect(5, H - 8, shield, 4, shield > 40 ? RGB(60, 220, 120) : RGB(240, 70, 60));
		text(110, H - 9, "SHIELD", 1, RGB(150, 150, 180));
	}
	if (state == S_PLAY && bannerT > 0) { snprintf(buf, sizeof buf, "LEVEL %d", level); textc(W / 2, 90, buf, 3, RGB(120, 255, 220)); }
	if (state == S_TITLE) {
		textc(W / 2, 55, "TYPESHOT", 5, RGB(100, 230, 255));
		textc(W / 2, 100, "TYPE THE WORDS TO SHOOT THEM DOWN", 1, RGB(200, 200, 230));
		textc(W / 2, 125, "PLUG IN A USB KEYBOARD", 1, RGB(255, 220, 120));
		textc(W / 2, 160, (frames / 30) & 1 ? "PRESS ENTER TO START" : "", 2, RGB(255, 255, 255));
		textc(W / 2, 185, "WIIMOTE A ALSO STARTS - HOME EXITS", 1, RGB(140, 140, 170));
		textc(W / 2, 195, sound_muted() ? "SOUND OFF - TAB TO TOGGLE" : "SOUND ON - TAB TO MUTE", 1, RGB(140, 140, 170));
		if (hiscore) { snprintf(buf, sizeof buf, "HI-SCORE %d", hiscore); textc(W / 2, 205, buf, 1, RGB(200, 200, 255)); }
	}
	if (state == S_PAUSE) { textc(W / 2, 100, "PAUSED", 4, RGB(255, 255, 255)); textc(W / 2, 140, "ENTER TO RESUME", 1, RGB(200, 200, 230)); }
	if (state == S_OVER) {
		textc(W / 2, 60, "GAME OVER", 4, RGB(255, 90, 90));
		snprintf(buf, sizeof buf, "SCORE %d", score); textc(W / 2, 105, buf, 2, RGB(255, 255, 255));
		int tot = keysHit + keysMiss, acc = tot ? keysHit * 100 / tot : 0;
		int wpm = frames > 0 ? (int)(keysHit / 5.f / (frames / 3600.f)) : 0;
		snprintf(buf, sizeof buf, "ACCURACY %d%%   WPM %d", acc, wpm); textc(W / 2, 135, buf, 1, RGB(200, 200, 255));
		snprintf(buf, sizeof buf, "WORDS %d   BEST COMBO %d", kills, bestCombo); textc(W / 2, 150, buf, 1, RGB(200, 200, 255));
		snprintf(buf, sizeof buf, "HI-SCORE %d", hiscore); textc(W / 2, 165, buf, 1, RGB(255, 220, 90));
		textc(W / 2, 190, "PRESS ENTER TO PLAY AGAIN", 1, RGB(255, 255, 255));
	}
	if (!kbOn && state != S_PLAY) textc(W / 2, 225, "NO KEYBOARD DETECTED", 1, RGB(255, 90, 90));
}

/* ---------------- video ---------------- */
static void present(void) {
	int H2 = rmode->xfbHeight, W2 = rmode->fbWidth / 2;
	u32 *dst = xfb[fbi];
	int ox = (shake ? rnd(5) - 2 : 0);
	for (int y = 0, last = -1; y < H2; y++) {
		int sy = y * H / H2;
		if (sy != last) {
			last = sy;
			for (int x = 0; x < W; x++) {
				u32 c = fb[sy][(x + ox + W) % W];
				int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
				int Y = (299 * r + 587 * g + 114 * b) / 1000;
				int cb = 128 + (-169 * r - 331 * g + 500 * b) / 1000;
				int cr = 128 + (500 * r - 419 * g - 81 * b) / 1000;
				if (cb < 0) cb = 0;
				if (cb > 255) cb = 255;
				if (cr < 0) cr = 0;
				if (cr > 255) cr = 255;
				yuvrow[x] = (Y << 24) | (cb << 16) | (Y << 8) | cr;
			}
		}
		u32 *row = dst + y * W2;
		for (int x = 0; x < W2; x++) row[x] = yuvrow[x * W / W2];
	}
	VIDEO_SetNextFramebuffer(dst);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	fbi ^= 1;
}

static void startPressed(void) {
	if (state == S_TITLE || state == S_OVER) newGame();
	else if (state == S_PAUSE) state = S_PLAY;
}

int main(void) {
	VIDEO_Init();
	WPAD_Init();
	KEYBOARD_Init(NULL);
	sound_init();
	rmode = VIDEO_GetPreferredMode(NULL);
	xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	VIDEO_Configure(rmode);
	VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer(rmode, xfb[1], COLOR_BLACK);
	VIDEO_SetNextFramebuffer(xfb[0]);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

	for (nwords = 0; WORDS[nwords]; nwords++) ;
	rng ^= SYS_Time();
	for (int i = 0; i < 90; i++) { st[i].x = rnd(W); st[i].y = rnd(H); st[i].v = 0.2f + rnd(100) / 100.f * 1.2f; }

	for (;;) {
		WPAD_ScanPads();
		u32 d = WPAD_ButtonsDown(0);
		if (d & WPAD_BUTTON_HOME) break;
		if (d & WPAD_BUTTON_A) startPressed();
		if (d & WPAD_BUTTON_MINUS) sound_toggle_mute();
		if (d & WPAD_BUTTON_PLUS) { if (state == S_PLAY) state = S_PAUSE; else if (state == S_PAUSE) state = S_PLAY; }

		keyboard_event ke;
		while (KEYBOARD_GetEvent(&ke)) {
			if (ke.type == KEYBOARD_CONNECTED) kbOn = 1;
			else if (ke.type == KEYBOARD_DISCONNECTED) kbOn = 0;
			else if (ke.type == KEYBOARD_PRESSED) {
				kbOn = 1;
				if (ke.symbol == KS_Return) startPressed();
				else if (ke.symbol == KS_Escape) { if (state == S_PLAY) state = S_PAUSE; else if (state == S_PAUSE) state = S_PLAY; }
				else if (ke.symbol == KS_Tab) sound_toggle_mute();
				else if (state == S_PLAY && ke.symbol < 128) typeKey((char)ke.symbol);
			}
		}
		update();
		render();
		present();
	}
	KEYBOARD_Deinit();
	return 0;
}
