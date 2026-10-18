/*
 * ZType for the Wii: setup, input and the frame loop.
 *
 * The game itself (game.c), the menus (menus.c) and the GX canvas renderer (gx_render.c) all work in
 * the 480x720 canvas of zty.pe. This file lays that canvas out in the page, draws the page behind
 * it and turns keyboard / controller input (via libinput-wii) into the one-frame flags of ig.input.
 */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>
#include <input_wii.h>
#include <wiiupdate.h>
#include <stdlib.h>
#include <string.h>

#include "zt.h"
#include "game.h"
#include "menus.h"
#include "stats.h"
#include "sound.h"

double zt_time;
float zt_tick;
Input zt_in;

static volatile int quit;
static void on_reset(u32 irq, void *ctx) { (void)irq; (void)ctx; quit = 1; }
static void on_power(void) { quit = 1; }

/* ---------------------------------------------------------------- the page around the canvas */

#define SHADOW_W 30.0f

/* canvas { box-shadow: 0 0 30px black }: a gaussian falloff, drawn in a few linear strips */
static void draw_canvas_shadow(float cx) {
    static const float a[5] = {0.5f, 0.31f, 0.16f, 0.067f, 0.0f};
    const float step = SHADOW_W / 4;
    for (int k = 0; k < 4; k++) {
        u32 near_c = (u32)(a[k] * 255 + 0.5f), far_c = (u32)(a[k + 1] * 255 + 0.5f);
        gx_quad_grad(cx - step * (k + 1), 0, step, PAGE_H, far_c, near_c);
        gx_quad_grad(cx + CANVAS_W + step * k, 0, step, PAGE_H, near_c, far_c);
    }
}

/* ---------------------------------------------------------------- update popup */

static wiiupdate_popup upd;
static int upd_open;

static void draw_update_popup(void) {
    float pw = (float)gx_page_width(), w = 600, h = upd.progress >= 0 || upd.detail[0] ? 210 : 176;
    float x = (pw - w) / 2, y = (PAGE_H - h) / 2;
    gx_save();
    gx_alpha(0.6f);
    gx_fill(0, 0, pw, PAGE_H, 0x000000);
    gx_alpha(0.95f);
    gx_fill(x, y, w, h, 0x0f2a33);
    gx_alpha(1);
    gx_fill(x, y, w, 3, 0x6fd3d3);
    sans_text(30, upd.title, pw / 2, y + 44, ALIGN_CENTER, 0xffffff);
    sans_text(18, upd.text, pw / 2, y + 92, ALIGN_CENTER, 0xd8e8e8);
    float row = y + 128;
    if (upd.progress >= 0) {
        gx_fill(x + 60, row, w - 120, 12, 0x2a4a52);
        gx_fill(x + 60, row, (w - 120) * upd.progress / 100.0f, 12, 0x6fd3d3);
        row += 36;
    } else if (upd.detail[0]) {
        sans_text(18, upd.detail, pw / 2, row, ALIGN_CENTER, 0x6fd3d3);
        row += 36;
    }
    sans_text(18, upd.hint, pw / 2, row + 4, ALIGN_CENTER, 0xa0b8b8);
    gx_restore();
}

static void draw_page(void) {
    int pw = gx_page_width();
    gx_image_tiled(TEX_PAGE, 0, 0, (float)pw, PAGE_H, 512, 512, 0, 0);

    float cx = (float)gx_canvas_x();
    draw_canvas_shadow(cx);

    gx_save();
    gx_clip_canvas(1);
    gx_translate(cx, 0);
    game_draw();
    gx_restore();
    gx_clip_canvas(0);

    menu_draw_overlay();
    if (upd_open) draw_update_popup();
}

/* ---------------------------------------------------------------- input */

static void poll_input(void) {
    memset(&zt_in, 0, sizeof(zt_in));
    zt_in.mouse_x = zt_in.mouse_y = -1;

    INPUT_Update();
    u32 d = INPUT_ButtonsDown();
    if (d & WPAD_BUTTON_HOME) quit = 1;
    if (d & WPAD_BUTTON_A) zt_in.ok = 1;
    if (d & WPAD_BUTTON_B) zt_in.enter = 1;
    if (d & WPAD_BUTTON_PLUS) zt_in.menu = 1;
    if (d & WPAD_BUTTON_MINUS) zt_in.back = 1;
    if (d & WPAD_BUTTON_1) zt_in.info = 1;
    if (d & WPAD_BUTTON_UP) zt_in.up = 1;
    if (d & WPAD_BUTTON_DOWN) zt_in.down = 1;
    if (d & WPAD_BUTTON_LEFT) zt_in.left = 1;
    if (d & WPAD_BUTTON_RIGHT) zt_in.right = 1;

    input_key_event ke;
    while (INPUT_KeyPop(&ke)) {
        switch (ke.code) {
        case 0x28: /* Enter */
        case 0x58: zt_in.ok = 1; zt_in.enter = 1; break; /* keypad Enter */
        case 0x2C: zt_in.ok = 1; break;                  /* Space */
        case 0x29: zt_in.menu = 1; break;                /* Escape */
        case 0x2A: zt_in.back = 1; break;                /* Backspace */
        case 0x52: zt_in.up = 1; break;
        case 0x51: zt_in.down = 1; break;
        case 0x50: zt_in.left = 1; break;
        case 0x4F: zt_in.right = 1; break;
        default:
            if (ke.ch == 'i' || ke.ch == 'I') zt_in.info = 1;
            if (ke.ch > 32 && ke.ch < 127 && !upd_open) game_key_letter(ke.ch);
            break;
        }
    }
}

/* ---------------------------------------------------------------- scripted run for screenshot tests */

#ifdef ZT_TEST_SCRIPT
enum { A_OK = 1, A_MENU, A_INFO, A_UP, A_DOWN, A_RIGHT, A_WAIT_GAME_OVER, A_AUTO_ON, A_AUTO_OFF };
static const struct { float wait; int action; } script[] = {
    { 6, A_OK },            /* new game */
    { 6, A_AUTO_ON },       /* an autopilot types the words */
    { 14, A_AUTO_OFF },
    { 3, A_MENU },          /* pause */
    { 2, A_RIGHT },
    { 2, A_MENU },          /* resume */
    { 0, A_WAIT_GAME_OVER },
    { 8, A_OK },            /* back to the title */
    { 4, A_INFO },          /* credits */
    { 3, A_OK },
    { 3, A_DOWN },
    { 0.3f, A_DOWN },
    { 0.3f, A_OK },         /* my stats */
    { 3, A_OK },            /* close */
    { 2, A_UP },
    { 0.3f, A_OK },         /* settings */
    { 4, A_RIGHT },
    { 2, A_MENU },          /* back to the title */
};

static void run_script(void) {
    static int step, autopilot;
    static double t0, last_key;
    if (autopilot && zt_time - last_key > 0.14) {
        char c = game_test_next_letter();
        if (c) game_key_letter(c);
        last_key = zt_time;
    }
    if (step >= (int)(sizeof(script) / sizeof(script[0]))) return;
    if (script[step].action == A_WAIT_GAME_OVER) {
        if (zg.mode == MODE_GAME_OVER && menu_kind() == MENU_GAMEOVER) { step++; t0 = zt_time; }
        return;
    }
    if (zt_time - t0 < script[step].wait) return;
    switch (script[step].action) {
    case A_OK: zt_in.ok = 1; break;
    case A_MENU: zt_in.menu = 1; break;
    case A_INFO: zt_in.info = 1; break;
    case A_UP: zt_in.up = 1; break;
    case A_DOWN: zt_in.down = 1; break;
    case A_RIGHT: zt_in.right = 1; break;
    case A_AUTO_ON: autopilot = 1; break;
    case A_AUTO_OFF: autopilot = 0; break;
    }
    step++;
    t0 = zt_time;
}
#endif

/* ---------------------------------------------------------------- main */

int main(int argc, char **argv) {
    /* Every controller as Wii Remote buttons; the keyboard types (no key-to-button map). */
    INPUT_InitEx(INPUT_INIT_NO_POINTER | INPUT_INIT_NO_MOUSE);
    INPUT_KeyboardMapClear();
    SYS_SetResetCallback(on_reset);
    SYS_SetPowerCallback(on_power);

    sound_init();
    stats_init();
    gx_init();
    sound_set_volumes(stats_sound_volume(), stats_music_volume());

#ifdef ZT_TEST_SCRIPT
    srand(1234);
    for (int i = 0; i < 24; i++) stats_add_game(60 + i * 9 + (int)(frand() * 45), 3 + i / 4, 8 + i, 70 + frand() * 25);
#else
    srand((unsigned)SYS_Time());
#endif
    game_init();
    WIIUPDATE_SetAppPath(argc > 0 ? argv[0] : NULL);
    WIIUPDATE_Start("quatric", "zType-wii", APP_VERSION);

    u64 last = gettime();
    while (!quit) {
        u64 now = gettime();
        float dt = (float)ticks_to_microsecs(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        zt_tick = dt;
        zt_time += dt;

        poll_input();
        {
            /* The update popup has the controls while it is up (Enter = A, Escape = B too). */
            u32 pd = INPUT_ButtonsDown();
            if (zt_in.ok) pd |= WPAD_BUTTON_A;
            if (zt_in.menu) pd |= WPAD_BUTTON_B;
            int was_open = upd_open;
            int ust = WIIUPDATE_Popup(pd, &upd);
            if (ust == WIIUPDATE_POPUP_EXIT) quit = 1;
            upd_open = ust == WIIUPDATE_POPUP_OPEN;
            if (upd_open || was_open) {
                memset(&zt_in, 0, sizeof(zt_in));
                zt_in.mouse_x = zt_in.mouse_y = -1;
            }
        }
#ifdef ZT_TEST_SCRIPT
        run_script();
#endif
        if (zt_in.enter) game_spawn_emp();
        if (zt_in.back) game_cancel_target();

        if (menu_modal_open()) menu_update();
        else game_update();

        gx_begin_frame();
        draw_page();
        gx_end_frame();
    }

    INPUT_Shutdown();
    return 0;
}
