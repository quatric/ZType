/*
 * ZType for the Wii: setup, input and the frame loop.
 *
 * The game itself (game.c), the menus (menus.c) and the GX canvas renderer (gx_render.c) all work in
 * the 480x720 canvas of zty.pe. This file lays that canvas out in the page, draws the page behind
 * it and turns keyboard / Wii remote input into the one-frame flags of ig.input.
 */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>
#include <wiikeyboard/keyboard.h>
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
}

/* ---------------------------------------------------------------- music */

/* Like the website: a new game starts the next random track (ig.music.next()), it keeps playing
 * through the pause menu and fades out when the game is over (ig.music.fadeOut(1)). */
static void update_music(void) {
    static int prev = -1;
    if ((int)zg.mode != prev) {
        if (zg.mode == MODE_GAME) sound_music_start();
        else if (zg.mode == MODE_GAME_OVER) sound_music_stop(1.0f);
        else if (prev == MODE_GAME) sound_music_stop(0.3f);   /* quit to the title from the pause menu */
        prev = (int)zg.mode;
    }
    sound_update(zt_tick);
}

/* ---------------------------------------------------------------- input */

static void poll_input(void) {
    memset(&zt_in, 0, sizeof(zt_in));
    zt_in.mouse_x = zt_in.mouse_y = -1;

    WPAD_ScanPads();
    u32 d = WPAD_ButtonsDown(0);
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

    keyboard_event ke;
    while (KEYBOARD_GetEvent(&ke)) {
        if (ke.type != KEYBOARD_PRESSED) continue;
        switch (ke.symbol) {
        case KS_Return:
        case KS_KP_Enter: zt_in.ok = 1; zt_in.enter = 1; break;
        case KS_space: zt_in.ok = 1; break;
        case KS_Escape: zt_in.menu = 1; break;
        case KS_BackSpace: zt_in.back = 1; break;
        case KS_Up: zt_in.up = 1; break;
        case KS_Down: zt_in.down = 1; break;
        case KS_Left: zt_in.left = 1; break;
        case KS_Right: zt_in.right = 1; break;
        default:
            if (ke.symbol == 'i' || ke.symbol == 'I') zt_in.info = 1;
            if (ke.symbol > 32 && ke.symbol < 127) game_key_letter((char)ke.symbol);
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
    (void)argc; (void)argv;
    WPAD_Init();
    KEYBOARD_Init(NULL);
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

    u64 last = gettime();
    while (!quit) {
        u64 now = gettime();
        float dt = (float)ticks_to_microsecs(now - last) / 1000000.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        zt_tick = dt;
        zt_time += dt;

        poll_input();
#ifdef ZT_TEST_SCRIPT
        run_script();
#endif
        if (zt_in.enter) game_spawn_emp();
        if (zt_in.back) game_cancel_target();

        if (menu_modal_open()) menu_update();
        else game_update();
        update_music();

        gx_begin_frame();
        draw_page();
        gx_end_frame();
    }

    KEYBOARD_Deinit();
    return 0;
}
