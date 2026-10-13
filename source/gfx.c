/* Software 2D primitives: blending, glow, anti-aliased polygons and TrueType text. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#include "gfx.h"

uint32_t fb[SH][SW];
static int cx0 = 0, cy0 = 0, cx1 = SW, cy1 = SH;

void gfx_clip(int x0, int y0, int x1, int y1) {
	cx0 = clampi(x0, 0, SW); cy0 = clampi(y0, 0, SH);
	cx1 = clampi(x1, 0, SW); cy1 = clampi(y1, 0, SH);
}
void gfx_noclip(void) { cx0 = 0; cy0 = 0; cx1 = SW; cy1 = SH; }

void blend(int x, int y, uint32_t c, int a) {
	if (x < cx0 || x >= cx1 || y < cy0 || y >= cy1 || a <= 0) return;
	if (a >= 255) { fb[y][x] = c; return; }
	uint32_t d = fb[y][x];
	int dr = CR(d), dg = CG(d), db = CB(d);
	dr += ((int)CR(c) - dr) * a / 255;
	dg += ((int)CG(c) - dg) * a / 255;
	db += ((int)CB(c) - db) * a / 255;
	fb[y][x] = RGB(dr, dg, db);
}
void addpx(int x, int y, int r, int g, int b) {
	if (x < cx0 || x >= cx1 || y < cy0 || y >= cy1) return;
	uint32_t d = fb[y][x];
	r += CR(d); g += CG(d); b += CB(d);
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	fb[y][x] = RGB(r, g, b);
}
void fill_rect(int x, int y, int w, int h, uint32_t c, int a) {
	int x0 = x < cx0 ? cx0 : x, x1 = x + w > cx1 ? cx1 : x + w;
	int y0 = y < cy0 ? cy0 : y, y1 = y + h > cy1 ? cy1 : y + h;
	for (int j = y0; j < y1; j++) for (int i = x0; i < x1; i++) blend(i, j, c, a);
}

void glow(float cx, float cy, float rad, uint32_t c, float k) {
	int x0 = (int)floorf(cx - rad), x1 = (int)ceilf(cx + rad);
	int y0 = (int)floorf(cy - rad), y1 = (int)ceilf(cy + rad);
	if (x0 < cx0) x0 = cx0;
	if (y0 < cy0) y0 = cy0;
	if (x1 > cx1) x1 = cx1;
	if (y1 > cy1) y1 = cy1;
	float r2 = rad * rad;
	for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
		float dx = x + .5f - cx, dy = y + .5f - cy, d2 = dx * dx + dy * dy;
		if (d2 >= r2) continue;
		float f = 1.f - d2 / r2; f = f * f * k;
		addpx(x, y, (int)(CR(c) * f), (int)(CG(c) * f), (int)(CB(c) * f));
	}
}
void ring(float cx, float cy, float rad, float thick, uint32_t c, float k) {
	float ext = rad + thick;
	int x0 = (int)floorf(cx - ext), x1 = (int)ceilf(cx + ext);
	int y0 = (int)floorf(cy - ext), y1 = (int)ceilf(cy + ext);
	if (x0 < cx0) x0 = cx0;
	if (y0 < cy0) y0 = cy0;
	if (x1 > cx1) x1 = cx1;
	if (y1 > cy1) y1 = cy1;
	for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
		float dx = x + .5f - cx, dy = y + .5f - cy;
		float d = fabsf(sqrtf(dx * dx + dy * dy) - rad);
		if (d >= thick) continue;
		float f = (1.f - d / thick) * k;
		addpx(x, y, (int)(CR(c) * f), (int)(CG(c) * f), (int)(CB(c) * f));
	}
}
void disc(float cx, float cy, float rad, uint32_t c, int a) {
	int x0 = (int)floorf(cx - rad - 1), x1 = (int)ceilf(cx + rad + 1);
	int y0 = (int)floorf(cy - rad - 1), y1 = (int)ceilf(cy + rad + 1);
	for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
		float dx = x + .5f - cx, dy = y + .5f - cy;
		float cov = clampf(rad + .5f - sqrtf(dx * dx + dy * dy), 0.f, 1.f);
		blend(x, y, c, (int)(a * cov));
	}
}
void line_aa(float x0, float y0, float x1, float y1, float w, uint32_t c, int a, int add) {
	float h = w * .5f + 1.f;
	int bx0 = (int)floorf(fminf(x0, x1) - h), bx1 = (int)ceilf(fmaxf(x0, x1) + h);
	int by0 = (int)floorf(fminf(y0, y1) - h), by1 = (int)ceilf(fmaxf(y0, y1) + h);
	float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
	for (int y = by0; y < by1; y++) for (int x = bx0; x < bx1; x++) {
		float px = x + .5f - x0, py = y + .5f - y0;
		float t = l2 > 0 ? clampf((px * dx + py * dy) / l2, 0.f, 1.f) : 0.f;
		float ex = px - dx * t, ey = py - dy * t;
		float cov = clampf(w * .5f + .5f - sqrtf(ex * ex + ey * ey), 0.f, 1.f);
		if (cov <= 0) continue;
		if (add) { float f = cov * a / 255.f; addpx(x, y, (int)(CR(c) * f), (int)(CG(c) * f), (int)(CB(c) * f)); }
		else blend(x, y, c, (int)(a * cov));
	}
}

/* ---------------- polygons ---------------- */
#define MAXEDGE 256
void poly_fill(const float *pts, const int *cnt, int nc, uint32_t c, int a, int add, ColorFn fn, void *u) {
	static float E[MAXEDGE][4];
	static float cov[SW + 2];
	int ne = 0, k = 0;
	float miny = 1e9f, maxy = -1e9f, minx = 1e9f, maxx = -1e9f;
	for (int ci = 0; ci < nc; ci++) {
		int n = cnt[ci];
		for (int i = 0; i < n; i++) {
			float x0 = pts[(k + i) * 2], y0 = pts[(k + i) * 2 + 1];
			float x1 = pts[(k + (i + 1) % n) * 2], y1 = pts[(k + (i + 1) % n) * 2 + 1];
			if (x0 < minx) minx = x0;
			if (x0 > maxx) maxx = x0;
			if (y0 < miny) miny = y0;
			if (y0 > maxy) maxy = y0;
			if (y0 == y1 || ne >= MAXEDGE) continue;
			if (y0 < y1) { E[ne][0] = x0; E[ne][1] = y0; E[ne][2] = x1; E[ne][3] = y1; }
			else { E[ne][0] = x1; E[ne][1] = y1; E[ne][2] = x0; E[ne][3] = y0; }
			ne++;
		}
		k += n;
	}
	int ys = (int)floorf(miny), ye = (int)ceilf(maxy);
	int xs0 = (int)floorf(minx), xs1 = (int)ceilf(maxx);
	if (xs0 < cx0) xs0 = cx0;
	if (xs1 > cx1) xs1 = cx1;
	if (ys < cy0) ys = cy0;
	if (ye > cy1) ye = cy1;
	for (int y = ys; y < ye; y++) {
		for (int x = xs0; x <= xs1; x++) cov[x] = 0;
		for (int s = 0; s < 4; s++) {
			float sy = y + (s + .5f) / 4.f, xs[64];
			int nx = 0;
			for (int e = 0; e < ne && nx < 64; e++)
				if (E[e][1] <= sy && sy < E[e][3])
					xs[nx++] = E[e][0] + (sy - E[e][1]) * (E[e][2] - E[e][0]) / (E[e][3] - E[e][1]);
			for (int i = 1; i < nx; i++) { float v = xs[i]; int j = i - 1; while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; } xs[j + 1] = v; }
			for (int i = 0; i + 1 < nx; i += 2) {
				float a0 = xs[i], a1 = xs[i + 1];
				if (a0 < xs0) a0 = (float)xs0;
				if (a1 > xs1) a1 = (float)xs1;
				if (a1 <= a0) continue;
				int p0 = (int)floorf(a0), p1 = (int)floorf(a1);
				if (p0 == p1) { cov[p0] += (a1 - a0) * .25f; continue; }
				cov[p0] += (p0 + 1 - a0) * .25f;
				for (int p = p0 + 1; p < p1; p++) cov[p] += .25f;
				cov[p1] += (a1 - p1) * .25f;
			}
		}
		uint32_t col = fn ? fn(y + .5f, u) : c;
		for (int x = xs0; x <= xs1; x++) {
			if (cov[x] <= 0.004f) continue;
			if (add) { float f = cov[x] * a / 255.f; addpx(x, y, (int)(CR(col) * f), (int)(CG(col) * f), (int)(CB(col) * f)); }
			else blend(x, y, col, (int)(a * clampf(cov[x], 0.f, 1.f)));
		}
	}
}

/* ---------------- fonts ---------------- */
typedef struct { uint8_t *bm; int w, h, xo, yo; float adv; } Glyph;
struct Font { Glyph g[95]; float ascent, height; };
Font *fWord, *fMenu, *fTitle, *fSmall, *fTiny, *fFoot, *fMenuS;

static Font *font_new(const unsigned char *ttf, float px) {
	stbtt_fontinfo info;
	Font *f = calloc(1, sizeof *f);
	stbtt_InitFont(&info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0));
	float sc = stbtt_ScaleForPixelHeight(&info, px);
	int asc, desc, gap;
	stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
	f->ascent = asc * sc; f->height = (asc - desc) * sc;
	for (int i = 0; i < 95; i++) {
		int adv, lsb;
		stbtt_GetCodepointHMetrics(&info, 32 + i, &adv, &lsb);
		f->g[i].adv = adv * sc;
		if (i == 0) continue;
		f->g[i].bm = stbtt_GetCodepointBitmap(&info, sc, sc, 32 + i, &f->g[i].w, &f->g[i].h, &f->g[i].xo, &f->g[i].yo);
	}
	return f;
}

void gfx_init(const unsigned char *light, const unsigned char *regular, const unsigned char *medium) {
	fWord = font_new(regular, 13.2f);
	fMenu = font_new(light, 19.7f);
	fMenuS = font_new(light, 16.f);
	fTitle = font_new(medium, 17.8f);
	fSmall = font_new(regular, 10.3f);
	fTiny = font_new(regular, 8.5f);
	fFoot = font_new(regular, 9.f);
}

float text_w(Font *f, const char *s, float sp) {
	float w = 0;
	for (; *s; s++) { int i = (unsigned char)*s - 32; if (i >= 0 && i < 95) w += f->g[i].adv + sp; }
	return w - (w > 0 ? sp : 0);
}
float text_h(Font *f) { return f->height; }

static void text_impl(Font *f, float x, float y, const char *s, uint32_t c, int a, float sp, int align, int add) {
	float w = text_w(f, s, sp);
	if (align == AL_CENTER) x -= w * .5f;
	else if (align == AL_RIGHT) x -= w;
	float by = y + f->ascent;
	for (; *s; s++) {
		int i = (unsigned char)*s - 32;
		if (i < 0 || i >= 95) continue;
		Glyph *g = &f->g[i];
		if (g->bm) {
			int gx = (int)floorf(x + .5f) + g->xo, gy = (int)floorf(by + .5f) + g->yo;
			for (int j = 0; j < g->h; j++) for (int k = 0; k < g->w; k++) {
				int v = g->bm[j * g->w + k] * a / 255;
				if (!v) continue;
				if (add) addpx(gx + k, gy + j, CR(c) * v / 255, CG(c) * v / 255, CB(c) * v / 255);
				else blend(gx + k, gy + j, c, v);
			}
		}
		x += g->adv + sp;
	}
}
void text(Font *f, float x, float y, const char *s, uint32_t c, int a, float sp, int align) { text_impl(f, x, y, s, c, a, sp, align, 0); }
void text_add(Font *f, float x, float y, const char *s, uint32_t c, int a, float sp, int align) { text_impl(f, x, y, s, c, a, sp, align, 1); }
