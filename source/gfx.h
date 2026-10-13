#ifndef GFX_H
#define GFX_H
#include <stdint.h>

/* Screen buffer is 640x480; the ZType playfield is a 300x450 (2:3) column centred on it. */
#define SW 640
#define SH 480
#define FX 170
#define FY 2
#define FW 300
#define FH 450

#define RGB(r,g,b) ((uint32_t)(((r)<<16)|((g)<<8)|(b)))
#define CR(c) (((c)>>16)&255)
#define CG(c) (((c)>>8)&255)
#define CB(c) ((c)&255)

extern uint32_t fb[SH][SW];

typedef struct Font Font;
extern Font *fWord, *fMenu, *fTitle, *fSmall, *fTiny, *fFoot, *fMenuS;
enum { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };

void gfx_init(const unsigned char *light, const unsigned char *regular, const unsigned char *medium);
void gfx_clip(int x0, int y0, int x1, int y1);
void gfx_noclip(void);

void blend(int x, int y, uint32_t c, int a);
void addpx(int x, int y, int r, int g, int b);
void fill_rect(int x, int y, int w, int h, uint32_t c, int a);
void glow(float cx, float cy, float rad, uint32_t c, float k);
void ring(float cx, float cy, float rad, float thick, uint32_t c, float k);
void line_aa(float x0, float y0, float x1, float y1, float w, uint32_t c, int a, int add);
void disc(float cx, float cy, float rad, uint32_t c, int a);

/* Even-odd polygon fill. pts: flat x,y list; cnt: points per contour. */
typedef uint32_t (*ColorFn)(float y, void *u);
void poly_fill(const float *pts, const int *cnt, int nc, uint32_t c, int a, int add, ColorFn fn, void *u);

float text_w(Font *f, const char *s, float sp);
float text_h(Font *f);
void text(Font *f, float x, float y, const char *s, uint32_t c, int a, float sp, int align);
void text_add(Font *f, float x, float y, const char *s, uint32_t c, int a, float sp, int align);

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
#endif
