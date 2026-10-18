#include "gx_render.h"
#include <malloc.h>
#include <string.h>
#include <math.h>
#include <ogc/conf.h>

#include "tex_page_bin.h"
#include "tex_stars_bin.h"
#include "tex_gradient_bin.h"
#include "tex_grid_bin.h"
#include "tex_logo_bin.h"
#include "tex_tship_bin.h"
#include "tex_credits_bin.h"
#include "tex_huge_bin.h"
#include "tex_huge_glitch_bin.h"
#include "tex_gl_stripe_bin.h"
#include "tex_gl_code_bin.h"
#include "tex_gl_jpeg_bin.h"
#include "tex_gl_graph_bin.h"
#include "tex_gl_log_bin.h"
#include "tex_sprites_bin.h"
#include "tex_font_ui_bin.h"
#include "tex_font_enemy_bin.h"
#include "tex_font_sans_bin.h"

#define FIFO_SIZE (256 * 1024)

static void *frameBuffer[2] = {NULL, NULL};
static GXRModeObj *rmode;
static int fb_index = 0;
static void *fifo = NULL;

static int page_w = 960;
static float efb_sx;           /* EFB pixels per page unit, horizontally */

/* ---------------------------------------------------------------- textures */

typedef struct { const u8 *data; int w, h; u8 fmt, wrap; } TexDef;

#define TEXDEF(n, W, H, FMT, WRAP) { n##_bin, W, H, FMT, WRAP }
static const TexDef texdefs[TEX_COUNT] = {
    [TEX_PAGE]        = TEXDEF(tex_page,        TEX_PAGE_W,        TEX_PAGE_H,        GX_TF_RGBA8,  GX_REPEAT),
    [TEX_STARS]       = TEXDEF(tex_stars,       TEX_STARS_W,       TEX_STARS_H,       GX_TF_RGBA8,  GX_CLAMP),
    [TEX_GRADIENT]    = TEXDEF(tex_gradient,    TEX_GRADIENT_W,    TEX_GRADIENT_H,    GX_TF_RGBA8,  GX_CLAMP),
    [TEX_GRID]        = TEXDEF(tex_grid,        TEX_GRID_W,        TEX_GRID_H,        GX_TF_RGBA8,  GX_CLAMP),
    [TEX_LOGO]        = TEXDEF(tex_logo,        TEX_LOGO_W,        TEX_LOGO_H,        GX_TF_RGBA8,  GX_CLAMP),
    [TEX_TSHIP]       = TEXDEF(tex_tship,       TEX_TSHIP_W,       TEX_TSHIP_H,       GX_TF_RGBA8,  GX_CLAMP),
    [TEX_CREDITS]     = TEXDEF(tex_credits,     TEX_CREDITS_W,     TEX_CREDITS_H,     GX_TF_RGBA8,  GX_CLAMP),
    [TEX_HUGE]        = TEXDEF(tex_huge,        TEX_HUGE_W,        TEX_HUGE_H,        GX_TF_RGBA8,  GX_CLAMP),
    [TEX_HUGE_GLITCH] = TEXDEF(tex_huge_glitch, TEX_HUGE_GLITCH_W, TEX_HUGE_GLITCH_H, GX_TF_RGBA8,  GX_CLAMP),
    [TEX_GL_STRIPE]   = TEXDEF(tex_gl_stripe,   TEX_GL_STRIPE_W,   TEX_GL_STRIPE_H,   GX_TF_RGB565, GX_CLAMP),
    [TEX_GL_CODE]     = TEXDEF(tex_gl_code,     TEX_GL_CODE_W,     TEX_GL_CODE_H,     GX_TF_RGB565, GX_CLAMP),
    [TEX_GL_JPEG]     = TEXDEF(tex_gl_jpeg,     TEX_GL_JPEG_W,     TEX_GL_JPEG_H,     GX_TF_RGB565, GX_CLAMP),
    [TEX_GL_GRAPH]    = TEXDEF(tex_gl_graph,    TEX_GL_GRAPH_W,    TEX_GL_GRAPH_H,    GX_TF_RGB565, GX_CLAMP),
    [TEX_GL_LOG]      = TEXDEF(tex_gl_log,      TEX_GL_LOG_W,      TEX_GL_LOG_H,      GX_TF_RGB565, GX_CLAMP),
    [TEX_SPRITES]     = TEXDEF(tex_sprites,     TEX_SPRITES_W,     TEX_SPRITES_H,     GX_TF_RGBA8,  GX_CLAMP),
    [TEX_FONT_UI]     = TEXDEF(tex_font_ui,     TEX_FONT_UI_W,     TEX_FONT_UI_H,     GX_TF_RGBA8,  GX_CLAMP),
    [TEX_FONT_ENEMY]  = TEXDEF(tex_font_enemy,  TEX_FONT_ENEMY_W,  TEX_FONT_ENEMY_H,  GX_TF_RGBA8,  GX_CLAMP),
    [TEX_FONT_SANS]   = TEXDEF(tex_font_sans,   TEX_FONT_SANS_W,   TEX_FONT_SANS_H,   GX_TF_RGBA8,  GX_CLAMP),
    [TEX_WHITE]       = { NULL, 4, 4, GX_TF_RGBA8, GX_CLAMP },
};

static GXTexObj texobj[TEX_COUNT];
static u8 white_tile[64] ATTRIBUTE_ALIGN(32);

static void tex_setup(void) {
    memset(white_tile, 0xFF, sizeof(white_tile));
    DCFlushRange(white_tile, sizeof(white_tile));
    for (int i = 0; i < TEX_COUNT; i++) {
        const TexDef *d = &texdefs[i];
        const void *data = d->data ? d->data : white_tile;
        if (d->data) {
            u32 bytes = (u32)(((d->w + 3) / 4) * ((d->h + 3) / 4)) * (d->fmt == GX_TF_RGBA8 ? 64 : 32);
            DCFlushRange((void *)data, (bytes + 31) & ~31u);
        }
        GX_InitTexObj(&texobj[i], (void *)data, d->w, d->h, d->fmt, d->wrap, d->wrap, GX_FALSE);
        GX_InitTexObjFilterMode(&texobj[i], GX_LINEAR, GX_LINEAR);
    }
    GX_InvalidateTexAll();
}

/* ---------------------------------------------------------------- video + GX setup */

void gx_init(void) {
    VIDEO_Init();
    CONF_Init();
    rmode = VIDEO_GetPreferredMode(NULL);

    int wide = (CONF_GetAspectRatio() == CONF_ASPECT_16_9);
#ifdef ZT_FORCE_43
    wide = 0;
#endif
    if (wide) {
        rmode->viWidth = 678;
        rmode->viXOrigin = (VI_MAX_WIDTH_NTSC - 678) / 2;
        page_w = 1280;
    } else {
        page_w = 960;
    }
    efb_sx = (float)rmode->fbWidth / (float)page_w;

    frameBuffer[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    frameBuffer[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    VIDEO_ClearFrameBuffer(rmode, frameBuffer[0], COLOR_BLACK);   /* zeroed YUV would show as green */
    VIDEO_ClearFrameBuffer(rmode, frameBuffer[1], COLOR_BLACK);

    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(frameBuffer[fb_index]);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
    fb_index ^= 1;

    fifo = memalign(32, FIFO_SIZE);
    memset(fifo, 0, FIFO_SIZE);
    GX_Init(fifo, FIFO_SIZE);

    GXColor background = {0, 0, 0, 0xff};
    GX_SetCopyClear(background, 0x00ffffff);

    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    u32 xfbHeight = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, xfbHeight);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering, ((rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));

    if (rmode->aa)
        GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
    else
        GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

    GX_SetCullMode(GX_CULL_NONE);
    GX_CopyDisp(frameBuffer[fb_index], GX_TRUE);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);

    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    GX_SetNumChans(1);
    GX_SetNumTexGens(1);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

    Mtx44 proj;
    guOrtho(proj, 0, PAGE_H, 0, page_w, 0, 300);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);

    tex_setup();
    gx_reset();
}

int gx_page_width(void) { return page_w; }
int gx_canvas_x(void) { return (page_w - CANVAS_W) / 2; }

/* ---------------------------------------------------------------- canvas state */

typedef struct { float a, b, c, d, tx, ty; } Xf;   /* x' = a*x + c*y + tx, y' = b*x + d*y + ty */

#define STACK_MAX 16
static Xf ctm;
static Xf xstack[STACK_MAX];
static float alphastack[STACK_MAX];
static int lighterstack[STACK_MAX];
static int sp;
static float galpha;
static int is_lighter;
static int cur_tex;
static int mtx_dirty;
static int clip_on;

static void apply_blend(void) {
    if (is_lighter) GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
    else GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
}

static void apply_scissor(void) {
    if (clip_on) {
        float x0 = gx_canvas_x() * efb_sx, x1 = (gx_canvas_x() + CANVAS_W) * efb_sx;
        int l = (int)floorf(x0 + 0.001f), r = (int)ceilf(x1 - 0.001f);
        GX_SetScissor(l, 0, r - l, rmode->efbHeight);
    } else {
        GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    }
}

void gx_reset(void) {
    ctm = (Xf){1, 0, 0, 1, 0, 0};
    sp = 0;
    galpha = 1.0f;
    is_lighter = 0;
    clip_on = 0;
    cur_tex = -1;
    mtx_dirty = 1;
    apply_blend();
    apply_scissor();
}

void gx_save(void) {
    if (sp < STACK_MAX) { xstack[sp] = ctm; alphastack[sp] = galpha; lighterstack[sp] = is_lighter; sp++; }
}

void gx_restore(void) {
    if (sp > 0) {
        sp--;
        ctm = xstack[sp];
        galpha = alphastack[sp];
        mtx_dirty = 1;
        gx_lighter(lighterstack[sp]);
    }
}

void gx_translate(float x, float y) {
    ctm.tx += ctm.a * x + ctm.c * y;
    ctm.ty += ctm.b * x + ctm.d * y;
    mtx_dirty = 1;
}

void gx_scale(float sx, float sy) {
    ctm.a *= sx; ctm.b *= sx;
    ctm.c *= sy; ctm.d *= sy;
    mtx_dirty = 1;
}

void gx_rotate(float r) {
    float c = cosf(r), s = sinf(r);
    float a = ctm.a * c + ctm.c * s;
    float b = ctm.b * c + ctm.d * s;
    float cc = ctm.a * -s + ctm.c * c;
    float d = ctm.b * -s + ctm.d * c;
    ctm.a = a; ctm.b = b; ctm.c = cc; ctm.d = d;
    mtx_dirty = 1;
}

void gx_alpha(float a) { galpha = a; }
float gx_get_alpha(void) { return galpha; }

void gx_lighter(int on) {
    if (on != is_lighter) { is_lighter = on; apply_blend(); }
}

void gx_clip_canvas(int on) {
    if (on != clip_on) { clip_on = on; apply_scissor(); }
}

void gx_begin_frame(void) {
    gx_reset();
}

void gx_end_frame(void) {
    GX_DrawDone();
    GX_CopyDisp(frameBuffer[fb_index], GX_TRUE);
    VIDEO_SetNextFramebuffer(frameBuffer[fb_index]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    fb_index ^= 1;
}

/* ---------------------------------------------------------------- primitives */

static void flush_state(int tex) {
    if (mtx_dirty) {
        Mtx m;
        m[0][0] = ctm.a; m[0][1] = ctm.c; m[0][2] = 0; m[0][3] = ctm.tx;
        m[1][0] = ctm.b; m[1][1] = ctm.d; m[1][2] = 0; m[1][3] = ctm.ty;
        m[2][0] = 0;     m[2][1] = 0;     m[2][2] = 1; m[2][3] = -5.0f;
        GX_LoadPosMtxImm(m, GX_PNMTX0);
        mtx_dirty = 0;
    }
    if (tex != cur_tex) {
        GX_LoadTexObj(&texobj[tex], GX_TEXMAP0);
        cur_tex = tex;
    }
}

static u32 pack(u32 rgb, float a) {
    int ia = (int)(a * 255.0f + 0.5f);
    if (ia < 0) ia = 0;
    if (ia > 255) ia = 255;
    return (rgb << 8) | (u32)ia;
}

void gx_bind(int tex) { flush_state(tex); }

void gx_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 rgb) {
    u32 c = pack(rgb, galpha);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x, y);         GX_Color1u32(c); GX_TexCoord2f32(u0, v0);
    GX_Position2f32(x + w, y);     GX_Color1u32(c); GX_TexCoord2f32(u1, v0);
    GX_Position2f32(x + w, y + h); GX_Color1u32(c); GX_TexCoord2f32(u1, v1);
    GX_Position2f32(x, y + h);     GX_Color1u32(c); GX_TexCoord2f32(u0, v1);
    GX_End();
}

static void quad_tex(int tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1) {
    flush_state(tex);
    gx_quad(x, y, w, h, u0, v0, u1, v1, 0xFFFFFF);
}

void gx_quad_grad(float x, float y, float w, float h, u32 rgba_left, u32 rgba_right) {
    flush_state(TEX_WHITE);
    u32 l = (rgba_left & 0xFFFFFF00) | (u32)((rgba_left & 0xFF) * galpha + 0.5f);
    u32 r = (rgba_right & 0xFFFFFF00) | (u32)((rgba_right & 0xFF) * galpha + 0.5f);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x, y);         GX_Color1u32(l); GX_TexCoord2f32(0.5f, 0.5f);
    GX_Position2f32(x + w, y);     GX_Color1u32(r); GX_TexCoord2f32(0.5f, 0.5f);
    GX_Position2f32(x + w, y + h); GX_Color1u32(r); GX_TexCoord2f32(0.5f, 0.5f);
    GX_Position2f32(x, y + h);     GX_Color1u32(l); GX_TexCoord2f32(0.5f, 0.5f);
    GX_End();
}

void gx_image(int tex, float x, float y) {
    quad_tex(tex, x, y, texdefs[tex].w, texdefs[tex].h, 0, 0, 1, 1);
}

void gx_image_sized(int tex, float x, float y, float w, float h) {
    quad_tex(tex, x, y, w, h, 0, 0, 1, 1);
}

/* repeat a texture over a rectangle; the texture is drawn tile_w x tile_h, shifted by (off_x, off_y) */
void gx_image_tiled(int tex, float x, float y, float w, float h, float tile_w, float tile_h,
                    float off_x, float off_y) {
    quad_tex(tex, x, y, w, h, off_x / tile_w, off_y / tile_h, (off_x + w) / tile_w, (off_y + h) / tile_h);
}

void gx_spr(const Spr *s, float x, float y) {
    quad_tex(TEX_SPRITES, x, y, s->w, s->h,
             (float)s->x / TEX_SPRITES_W, (float)s->y / TEX_SPRITES_H,
             (float)(s->x + s->w) / TEX_SPRITES_W, (float)(s->y + s->h) / TEX_SPRITES_H);
}

void gx_spr_stretch(const Spr *s, float x, float y, float w, float h) {
    quad_tex(TEX_SPRITES, x, y, w, h,
             (float)s->x / TEX_SPRITES_W, (float)s->y / TEX_SPRITES_H,
             (float)(s->x + s->w) / TEX_SPRITES_W, (float)(s->y + s->h) / TEX_SPRITES_H);
}

/* ig.Image.drawTile on an atlas sprite sheet */
void gx_spr_tile(const Spr *s, int tile, int tw, int th, float x, float y, int flip_x, int flip_y) {
    int per_row = s->w / tw;
    int tx = s->x + (tile % per_row) * tw;
    int ty = s->y + (tile / per_row) * th;
    float u0 = (float)tx / TEX_SPRITES_W, u1 = (float)(tx + tw) / TEX_SPRITES_W;
    float v0 = (float)ty / TEX_SPRITES_H, v1 = (float)(ty + th) / TEX_SPRITES_H;
    if (flip_x) { float t = u0; u0 = u1; u1 = t; }
    if (flip_y) { float t = v0; v0 = v1; v1 = t; }
    quad_tex(TEX_SPRITES, x, y, tw, th, u0, v0, u1, v1);
}

void gx_fill(float x, float y, float w, float h, u32 rgb) {
    flush_state(TEX_WHITE);
    gx_quad(x, y, w, h, 0.5f, 0.5f, 0.5f, 0.5f, rgb);
}

void gx_line(float x0, float y0, float x1, float y1, float width, u32 rgb) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len * (width * 0.5f), ny = dx / len * (width * 0.5f);
    u32 c = pack(rgb, galpha);
    flush_state(TEX_WHITE);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x0 + nx, y0 + ny); GX_Color1u32(c); GX_TexCoord2f32(0.5f, 0.5f);
    GX_Position2f32(x0 - nx, y0 - ny); GX_Color1u32(c); GX_TexCoord2f32(0.5f, 0.5f);
    GX_Position2f32(x1 - nx, y1 - ny); GX_Color1u32(c); GX_TexCoord2f32(0.5f, 0.5f);
    GX_Position2f32(x1 + nx, y1 + ny); GX_Color1u32(c); GX_TexCoord2f32(0.5f, 0.5f);
    GX_End();
}

/* ---------------------------------------------------------------- impact.js fonts */

static int font_spacing(int font) { return font == FONT_18_WHITE ? -1 : -2; }

static float ui_line_width(int font, const char *s, int len) {
    const FontUI *f = FONT_UI_DEFS[font];
    int ls = font_spacing(font);
    float w = 0;
    for (int i = 0; i < len; i++) {
        int c = (unsigned char)s[i] - 32;
        if (c >= 0 && c < f->n) w += f->g[c].w + ls;
    }
    return w;
}

float ui_font_height(int font) { return (float)FONT_UI_DEFS[font]->height; }

float ui_text_width(int font, const char *s) {
    float best = 0;
    const char *line = s;
    for (;;) {
        const char *e = strchr(line, '\n');
        int len = e ? (int)(e - line) : (int)strlen(line);
        float w = ui_line_width(font, line, len);
        if (w > best) best = w;
        if (!e) break;
        line = e + 1;
    }
    return best;
}

static void ui_line(int font, const char *s, int len, float x, float y, int align) {
    const FontUI *f = FONT_UI_DEFS[font];
    int ls = font_spacing(font);
    if (align != ALIGN_LEFT) {
        float w = ui_line_width(font, s, len);
        x -= align == ALIGN_CENTER ? w / 2 : w;
    }
    flush_state(TEX_FONT_UI);
    for (int i = 0; i < len; i++) {
        int c = (unsigned char)s[i] - 32;
        if (c < 0 || c >= f->n) continue;
        const GlyphUI *g = &f->g[c];
        if (g->w > 0) {
            gx_quad(x, y, g->w, f->ch,
                    (float)g->x / TEX_FONT_UI_W, (float)g->y / TEX_FONT_UI_H,
                    (float)(g->x + g->w) / TEX_FONT_UI_W, (float)(g->y + f->ch) / TEX_FONT_UI_H, 0xFFFFFF);
        }
        x += g->w + ls;
    }
}

void ui_text(int font, const char *s, float x, float y, int align) {
    float lh = ui_font_height(font);
    const char *line = s;
    int row = 0;
    for (;;) {
        const char *e = strchr(line, '\n');
        int len = e ? (int)(e - line) : (int)strlen(line);
        ui_line(font, line, len, x, y + row * lh, align);
        if (!e) break;
        line = e + 1;
        row++;
    }
}

/* ---------------------------------------------------------------- avenir-22 enemy labels */

#define ENEMY_SPACING 0.5f

float enemy_text_width(const char *s) {
    float w = 0;
    for (; *s; s++) {
        int c = (unsigned char)*s - 32;
        if (c >= 0 && c < 223) w += ENEMY_FONT_METRICS[c].adv + ENEMY_SPACING;
    }
    return w;
}

void enemy_text(const char *s, float x, float y, int align, int active) {
    if (align != ALIGN_LEFT) {
        float w = enemy_text_width(s);
        x -= align == ALIGN_CENTER ? w / 2 : w;
    }
    flush_state(TEX_FONT_ENEMY);
    for (; *s; s++) {
        int c = (unsigned char)*s - 32;
        if (c < 0 || c >= 223) continue;
        const Glyph22 *g = &ENEMY_FONT_METRICS[c];
        int sy = g->sy + (active ? 112 : 0);
        if (g->sw > 0 && g->sh > 0) {
            gx_quad(x + g->ox, y + g->oy, g->sw, g->sh,
                    (float)g->sx / TEX_FONT_ENEMY_W, (float)sy / TEX_FONT_ENEMY_H,
                    (float)(g->sx + g->sw) / TEX_FONT_ENEMY_W, (float)(sy + g->sh) / TEX_FONT_ENEMY_H, 0xFFFFFF);
        }
        x += g->adv + ENEMY_SPACING;
    }
}

/* ---------------------------------------------------------------- sans-serif text */

static const GlyphSans *sans_table(int size) {
    return size <= 10 ? SANS_10 : size <= 12 ? SANS_12 : size <= 14 ? SANS_14 : SANS_28;
}

float sans_text_width(int size, const char *s) {
    const GlyphSans *t = sans_table(size);
    float w = 0;
    for (; *s; s++) {
        int c = (unsigned char)*s - 32;
        if (c >= 0 && c < 95) w += t[c].adv;
    }
    return w;
}

void sans_text(int size, const char *s, float x, float y, int align, u32 rgb) {
    const GlyphSans *t = sans_table(size);
    if (align != ALIGN_LEFT) {
        float w = sans_text_width(size, s);
        x -= align == ALIGN_CENTER ? w / 2 : w;
    }
    flush_state(TEX_FONT_SANS);
    for (; *s; s++) {
        int c = (unsigned char)*s - 32;
        if (c < 0 || c >= 95) continue;
        const GlyphSans *g = &t[c];
        if (g->w > 0) {
            gx_quad(x + g->xo, y + g->yo, g->w, g->h,
                    (float)g->x / TEX_FONT_SANS_W, (float)g->y / TEX_FONT_SANS_H,
                    (float)(g->x + g->w) / TEX_FONT_SANS_W, (float)(g->y + g->h) / TEX_FONT_SANS_H, rgb);
        }
        x += g->adv;
    }
}
