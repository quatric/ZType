#include "gx_render.h"
#include <malloc.h>
#include <string.h>
#include <math.h>

#include "tex_sprites_bin.h"
#include "tex_font_enemy_bin.h"
#include "tex_font_ui_bin.h"
#include "tex_title_bin.h"
#include "tex_stars_bin.h"
#include "tex_grid_bin.h"

#define FIFO_SIZE (256 * 1024)

static void *frameBuffer[2] = {NULL, NULL};
static GXRModeObj *rmode;
static int fb_index = 0;
static void *fifo = NULL;

static GXTexObj tex_sprites;
static GXTexObj tex_font_enemy;
static GXTexObj tex_font_ui;
static GXTexObj tex_title;
static GXTexObj tex_stars;
static GXTexObj tex_grid;

void gx_init(void) {
    VIDEO_Init();
    rmode = VIDEO_GetPreferredMode(NULL);

    frameBuffer[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    frameBuffer[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

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

    // Setup vertex descriptors
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

    GX_SetNumChans(1);
    GX_SetNumTexGens(1);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

    GX_InvalidateTexAll();

    // Initialize textures
    DCFlushRange((void*)tex_sprites_bin, tex_sprites_bin_size);
    GX_InitTexObj(&tex_sprites, (void*)tex_sprites_bin, 512, 512, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&tex_sprites, GX_LINEAR, GX_LINEAR);

    DCFlushRange((void*)tex_font_enemy_bin, tex_font_enemy_bin_size);
    GX_InitTexObj(&tex_font_enemy, (void*)tex_font_enemy_bin, 512, 224, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&tex_font_enemy, GX_LINEAR, GX_LINEAR);

    DCFlushRange((void*)tex_font_ui_bin, tex_font_ui_bin_size);
    GX_InitTexObj(&tex_font_ui, (void*)tex_font_ui_bin, 512, 512, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&tex_font_ui, GX_LINEAR, GX_LINEAR);

    DCFlushRange((void*)tex_title_bin, tex_title_bin_size);
    GX_InitTexObj(&tex_title, (void*)tex_title_bin, 512, 512, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&tex_title, GX_LINEAR, GX_LINEAR);

    DCFlushRange((void*)tex_stars_bin, tex_stars_bin_size);
    GX_InitTexObj(&tex_stars, (void*)tex_stars_bin, 512, 512, GX_TF_RGB565, GX_REPEAT, GX_REPEAT, GX_FALSE);
    GX_InitTexObjFilterMode(&tex_stars, GX_LINEAR, GX_LINEAR);

    DCFlushRange((void*)tex_grid_bin, tex_grid_bin_size);
    GX_InitTexObj(&tex_grid, (void*)tex_grid_bin, 512, 512, GX_TF_RGBA8, GX_REPEAT, GX_REPEAT, GX_FALSE);
    GX_InitTexObjFilterMode(&tex_grid, GX_LINEAR, GX_LINEAR);

    Mtx44 perspective;
    guOrtho(perspective, 0, SCREEN_H, 0, SCREEN_W, 0, 300);
    GX_LoadProjectionMtx(perspective, GX_ORTHOGRAPHIC);
}

void gx_begin_frame(void) {
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    GX_InvVtxCache();
    GX_InvalidateTexAll();

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

    Mtx GXmodelView2D;
    guMtxIdentity(GXmodelView2D);
    guMtxTransApply(GXmodelView2D, GXmodelView2D, 0.0f, 0.0f, -5.0f);
    GX_LoadPosMtxImm(GXmodelView2D, GX_PNMTX0);

    gx_set_blend_alpha();
}

void gx_end_frame(void) {
    GX_DrawDone();
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_CopyDisp(frameBuffer[fb_index], GX_TRUE);

    VIDEO_SetNextFramebuffer(frameBuffer[fb_index]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    fb_index ^= 1;
}

void gx_set_blend_alpha(void) {
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
}

void gx_set_blend_additive(void) {
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
}

void gx_bind_sprites(void) { GX_LoadTexObj(&tex_sprites, GX_TEXMAP0); }
void gx_bind_enemy_font(void) { GX_LoadTexObj(&tex_font_enemy, GX_TEXMAP0); }
void gx_bind_ui_font(void) { GX_LoadTexObj(&tex_font_ui, GX_TEXMAP0); }
void gx_bind_title(void) { GX_LoadTexObj(&tex_title, GX_TEXMAP0); }
void gx_bind_stars(void) { GX_LoadTexObj(&tex_stars, GX_TEXMAP0); }
void gx_bind_grid(void) { GX_LoadTexObj(&tex_grid, GX_TEXMAP0); }

void gx_draw_sprite(float x, float y, float w, float h,
                    float u0, float v0, float u1, float v1, u32 color) {
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x, y);
    GX_Color1u32(color);
    GX_TexCoord2f32(u0, v0);

    GX_Position2f32(x + w, y);
    GX_Color1u32(color);
    GX_TexCoord2f32(u1, v0);

    GX_Position2f32(x + w, y + h);
    GX_Color1u32(color);
    GX_TexCoord2f32(u1, v1);

    GX_Position2f32(x, y + h);
    GX_Color1u32(color);
    GX_TexCoord2f32(u0, v1);
    GX_End();
}

void gx_draw_sprite_rot(float cx, float cy, float w, float h, float angle_rad,
                        float u0, float v0, float u1, float v1, u32 color) {
    float cos_a = cosf(angle_rad);
    float sin_a = sinf(angle_rad);
    float hw = w * 0.5f;
    float hh = h * 0.5f;

    float x0 = -hw, y0 = -hh;
    float x1 =  hw, y1 = -hh;
    float x2 =  hw, y2 =  hh;
    float x3 = -hw, y3 =  hh;

    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(cx + x0 * cos_a - y0 * sin_a, cy + x0 * sin_a + y0 * cos_a);
    GX_Color1u32(color);
    GX_TexCoord2f32(u0, v0);

    GX_Position2f32(cx + x1 * cos_a - y1 * sin_a, cy + x1 * sin_a + y1 * cos_a);
    GX_Color1u32(color);
    GX_TexCoord2f32(u1, v0);

    GX_Position2f32(cx + x2 * cos_a - y2 * sin_a, cy + x2 * sin_a + y2 * cos_a);
    GX_Color1u32(color);
    GX_TexCoord2f32(u1, v1);

    GX_Position2f32(cx + x3 * cos_a - y3 * sin_a, cy + x3 * sin_a + y3 * cos_a);
    GX_Color1u32(color);
    GX_TexCoord2f32(u0, v1);
    GX_End();
}

void gx_draw_rect(float x, float y, float w, float h, u32 color) {
    gx_bind_sprites();
    float u = (SPRITE_BAR_BLUE_X + 1.0f) / 512.0f;
    float v = (SPRITE_BAR_BLUE_Y + 1.0f) / 512.0f;
    gx_draw_sprite(x, y, w, h, u, v, u, v, color);
}

void gx_draw_gradient(float x, float y, float w, float h, u32 top_color, u32 bot_color) {
    gx_bind_sprites();
    float u = (SPRITE_BAR_BLUE_X + 1.0f) / 512.0f;
    float v = (SPRITE_BAR_BLUE_Y + 1.0f) / 512.0f;
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x, y);
    GX_Color1u32(top_color);
    GX_TexCoord2f32(u, v);

    GX_Position2f32(x + w, y);
    GX_Color1u32(top_color);
    GX_TexCoord2f32(u, v);

    GX_Position2f32(x + w, y + h);
    GX_Color1u32(bot_color);
    GX_TexCoord2f32(u, v);

    GX_Position2f32(x, y + h);
    GX_Color1u32(bot_color);
    GX_TexCoord2f32(u, v);
    GX_End();
}

void gx_draw_line(float x0, float y0, float x1, float y1, float width, u32 color) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    float nx = -dy / len * (width * 0.5f);
    float ny =  dx / len * (width * 0.5f);

    gx_bind_sprites();
    float u = (SPRITE_BAR_BLUE_X + 1.0f) / 512.0f;
    float v = (SPRITE_BAR_BLUE_Y + 1.0f) / 512.0f;

    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2f32(x0 + nx, y0 + ny);
    GX_Color1u32(color);
    GX_TexCoord2f32(u, v);

    GX_Position2f32(x0 - nx, y0 - ny);
    GX_Color1u32(color);
    GX_TexCoord2f32(u, v);

    GX_Position2f32(x1 - nx, y1 - ny);
    GX_Color1u32(color);
    GX_TexCoord2f32(u, v);

    GX_Position2f32(x1 + nx, y1 + ny);
    GX_Color1u32(color);
    GX_TexCoord2f32(u, v);
    GX_End();
}

int gx_text_width_ui(const char *str, int font_type) {
    if (!str) return 0;
    const GlyphUI *glyphs = (font_type == FONT_UI_36_BLUE) ? FONT36_BLUE_GLYPHS :
                            (font_type == FONT_UI_36_WHITE) ? FONT36_WHITE_GLYPHS : FONT18_WHITE_GLYPHS;
    int w = 0;
    for (; *str; str++) {
        char ch = *str;
        if (ch < 32 || ch > 126) ch = ' ';
        int idx = ch - 32;
        w += glyphs[idx].adv;
    }
    return w;
}

void gx_draw_text_ui(float x, float y, const char *str, int font_type, u32 color) {
    if (!str) return;
    gx_bind_ui_font();
    const GlyphUI *glyphs = (font_type == FONT_UI_36_BLUE) ? FONT36_BLUE_GLYPHS :
                            (font_type == FONT_UI_36_WHITE) ? FONT36_WHITE_GLYPHS : FONT18_WHITE_GLYPHS;

    for (; *str; str++) {
        char ch = *str;
        if (ch < 32 || ch > 126) ch = ' ';
        int idx = ch - 32;
        const GlyphUI *g = &glyphs[idx];
        gx_draw_sprite(x, y, g->w, g->h, g->u0, g->v0, g->u1, g->v1, color);
        x += g->adv;
    }
}

void gx_draw_text_ui_center(float cx, float y, const char *str, int font_type, u32 color) {
    int w = gx_text_width_ui(str, font_type);
    gx_draw_text_ui(cx - w * 0.5f, y, str, font_type, color);
}

int gx_text_width_enemy(const char *str) {
    if (!str) return 0;
    int w = 0;
    for (; *str; str++) {
        uint8_t ch = (uint8_t)*str;
        if (ch < 32) ch = ' ';
        int idx = ch - 32;
        if (idx < 224) {
            w += ENEMY_FONT_METRICS[idx].adv + 1;
        } else {
            w += 10;
        }
    }
    return w;
}

void gx_draw_text_enemy(float x, float y, const char *str, int typed_count) {
    if (!str) return;
    gx_bind_enemy_font();

    int len = strlen(str);
    for (int i = 0; i < len; i++) {
        uint8_t ch = (uint8_t)str[i];
        if (ch < 32) ch = ' ';
        int idx = ch - 32;
        if (idx >= 224) idx = 0;

        const Glyph22 *g = &ENEMY_FONT_METRICS[idx];
        float u0 = g->sx / 512.0f;
        float v0 = (g->sy + (i < typed_count ? 112 : 0)) / 224.0f;
        float u1 = (g->sx + g->sw) / 512.0f;
        float v1 = (g->sy + g->sh + (i < typed_count ? 112 : 0)) / 224.0f;

        gx_draw_sprite(x + g->ox, y + g->oy, g->sw, g->sh, u0, v0, u1, v1, 0xFFFFFFFF);
        x += g->adv + 1;
    }
}
