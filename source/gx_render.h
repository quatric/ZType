#ifndef GX_RENDER_H
#define GX_RENDER_H

#include <gccore.h>
#include <stdint.h>
#include "assets.h"

/*
 * Rendering model: the screen is laid out like zty.pe in a browser window that is 720px tall.
 * The 480x720 game canvas sits in the middle of the page at 1:1 ("page space"); the whole page is
 * then scaled to the Wii frame (2/3 on a 640x480 frame). All game code draws in canvas pixels and
 * uses a small canvas-2D-like state machine (transform stack, globalAlpha, 'lighter' blending).
 */
#define CANVAS_W 480
#define CANVAS_H 720
#define PAGE_H 720

enum {
    TEX_PAGE, TEX_STARS, TEX_GRADIENT, TEX_GRID, TEX_LOGO, TEX_TSHIP, TEX_CREDITS,
    TEX_HUGE, TEX_HUGE_GLITCH, TEX_GL_STRIPE, TEX_GL_CODE, TEX_GL_JPEG, TEX_GL_GRAPH, TEX_GL_LOG,
    TEX_SPRITES, TEX_FONT_UI, TEX_FONT_ENEMY, TEX_FONT_SANS, TEX_WHITE,
    TEX_COUNT
};

enum { ALIGN_LEFT, ALIGN_RIGHT, ALIGN_CENTER };

void gx_init(void);
void gx_begin_frame(void);
void gx_end_frame(void);

/* page geometry */
int gx_page_width(void);      /* 960 on 4:3, 1280 on 16:9 */
int gx_canvas_x(void);        /* left edge of the canvas in page space */

/* canvas-like state */
void gx_reset(void);
void gx_save(void);
void gx_restore(void);
void gx_translate(float x, float y);
void gx_scale(float sx, float sy);
void gx_rotate(float rad);
void gx_alpha(float a);       /* ctx.globalAlpha = a */
float gx_get_alpha(void);
void gx_lighter(int on);      /* ctx.globalCompositeOperation = 'lighter' / 'source-over' */
void gx_clip_canvas(int on);  /* clip drawing to the 480x720 canvas rectangle */

/* primitives (all honour the current transform, alpha and blend mode) */
void gx_bind(int tex);
void gx_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, u32 rgb);
void gx_quad_grad(float x, float y, float w, float h, u32 rgba_left, u32 rgba_right);
void gx_image(int tex, float x, float y);                       /* whole texture, 1:1 */
void gx_image_sized(int tex, float x, float y, float w, float h);
void gx_image_tiled(int tex, float x, float y, float w, float h, float tile_w, float tile_h,
                    float off_x, float off_y);
void gx_spr(const Spr *s, float x, float y);                    /* atlas sprite, natural size */
void gx_spr_tile(const Spr *s, int tile, int tw, int th, float x, float y, int flip_x, int flip_y);
void gx_spr_stretch(const Spr *s, float x, float y, float w, float h);
void gx_fill(float x, float y, float w, float h, u32 rgb);      /* uses current alpha */
void gx_line(float x0, float y0, float x1, float y1, float width, u32 rgb);

/* impact.js bitmap fonts (ig.Font); sizes follow font.height */
float ui_text_width(int font, const char *s);
float ui_font_height(int font);
void ui_text(int font, const char *s, float x, float y, int align);

/* avenir-22 labels above enemies (ig.FontAvenirNext, letterSpacing 0.5) */
float enemy_text_width(const char *s);
void enemy_text(const char *s, float x, float y, int align, int active);

/* sans-serif text for the stats screen (size is 10, 12 or 28 px) */
float sans_text_width(int size, const char *s);
void sans_text(int size, const char *s, float x, float y, int align, u32 rgb);

#endif
