#ifndef GX_RENDER_H
#define GX_RENDER_H

#include <gccore.h>
#include <stdint.h>
#include "assets.h"

#define SCREEN_W 640
#define SCREEN_H 480

#define GAME_W 480
#define GAME_H 720

void gx_init(void);
void gx_begin_frame(void);
void gx_end_frame(void);

void gx_set_screen_viewport(void);
void gx_set_canvas_viewport(void);

void gx_set_blend_alpha(void);
void gx_set_blend_additive(void);

void gx_bind_sprites(void);
void gx_bind_enemy_font(void);
void gx_bind_ui_font(void);
void gx_bind_title(void);
void gx_bind_stars(void);
void gx_bind_grid(void);
void gx_bind_page(void);

void gx_draw_rect(float x, float y, float w, float h, u32 color);
void gx_draw_line(float x0, float y0, float x1, float y1, float width, u32 color);

void gx_draw_sprite(float x, float y, float w, float h,
                    float u0, float v0, float u1, float v1, u32 color);
void gx_draw_sprite_rot(float cx, float cy, float w, float h, float angle_rad,
                        float u0, float v0, float u1, float v1, u32 color);

int gx_text_width_ui(const char *str, int font_type);
void gx_draw_text_ui(float x, float y, const char *str, int font_type, u32 color);
void gx_draw_text_ui_center(float cx, float y, const char *str, int font_type, u32 color);

int gx_text_width_enemy(const char *str);
void gx_draw_text_enemy(float x, float y, const char *str, int typed_count);
void gx_draw_text_enemy_right(float right_x, float y, const char *str, int is_active);

void gx_draw_gradient(float x, float y, float w, float h, u32 top_color, u32 bot_color);
void gx_draw_hgradient(float x, float y, float w, float h, u32 left_color, u32 right_color);
void gx_draw_page_backdrop(void);

enum {
    FONT_UI_36_BLUE,
    FONT_UI_36_WHITE,
    FONT_UI_36_ORANGE,
    FONT_UI_18_WHITE
};

#endif
