/*
 * The menus of ZType (title, settings/pause, credits, game over) and the "my stats" card,
 * ported from the menu classes of the original game.
 */
#include "menus.h"
#include "game.h"
#include "stats.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *(*text)(void);
    void (*ok)(void);
    void (*left)(void);
    void (*right)(void);
    float y;        /* 0 = not set, the next free position is used */
    float alpha;    /* 0 = not set */
} MenuItem;

static struct {
    MenuKind kind;
    MenuItem items[6];
    int count, current;
    float scale, alpha, y, width;
    float clear_alpha;              /* clearColor: rgba(0,0,0,a) */
    float scroll;                   /* stars scroll (title, credits) */
    float transition;               /* title: 0..1 while a game starts */
    float player_y;
    Timer timer;                    /* game over: input is locked for 1.5s, graph animation */
    char msg_title[48], msg_text[320];
} M;

enum { MODAL_NONE, MODAL_OPEN, MODAL_CLOSING };
static int modal_state;             /* the "my stats" card */
static Timer modal_timer;
static float sound_volume = 0.5f, music_volume = 0.5f;

/* ---------------------------------------------------------------- item texts and actions */

static const char *t_new_game(void) { return "new game"; }
static const char *t_settings(void) { return "settings"; }
static const char *t_stats(void) { return "my stats"; }
static const char *t_load_text(void) { return game_has_custom_words() ? "try a different text" : "load your own text"; }
static const char *t_back(void) { return "back to title"; }
static const char *t_resume(void) { return "resume"; }
static const char *t_sound(void) { static char b[8]; snprintf(b, sizeof(b), "%d%%", (int)(sound_volume * 100 + 0.5f)); return b; }
static const char *t_music(void) { static char b[8]; snprintf(b, sizeof(b), "%d%%", (int)(music_volume * 100 + 0.5f)); return b; }

static void ok_new_game(void) { game_set_game(); }
static void ok_settings(void) { menu_open_settings(); }
static void ok_stats(void) { menu_open_stats(); }
static void ok_back(void) { game_set_title(); }
static void ok_resume(void) { menu_close(); }

static void apply_volumes(void) {
    sound_set_volumes(sound_volume, music_volume);
    stats_set_volumes(sound_volume, music_volume);
}
static void sound_left(void) { sound_volume = flimit(sound_volume - 0.1f, 0, 1); apply_volumes(); }
static void sound_right(void) { sound_volume = flimit(sound_volume + 0.1f, 0, 1); apply_volumes(); }
static void music_left(void) { music_volume = flimit(music_volume - 0.1f, 0, 1); apply_volumes(); }
static void music_right(void) { music_volume = flimit(music_volume + 0.1f, 0, 1); apply_volumes(); }

static void ok_load_text(void) {
    char *txt = stats_read_text();
    if (!txt) {
        menu_open_message("LOAD YOUR OWN TEXT",
                          "Put a text file named text.txt into\n"
                          "sd:/apps/typeshot/ on your SD card,\n"
                          "then pick this entry again.\n\n"
                          "The words of your text become the\n"
                          "enemies.");
        return;
    }
    int n = game_load_words(txt);
    free(txt);
    if (n == 0)
        menu_open_message("LOAD YOUR OWN TEXT",
                          "That text has fewer than 8 usable\n"
                          "words. Add some more to text.txt\n"
                          "and try again.");
}

/* ---------------------------------------------------------------- Menu base */

static void menu_reset(MenuKind kind, float scale) {
    memset(&M, 0, sizeof(M));
    M.kind = kind;
    M.scale = scale;
    M.alpha = 1;
    M.width = CANVAS_W;
    M.y = CANVAS_H / 4 + 160;
    timer_init(&M.timer, 0);
}

static void add_item(const char *(*text)(void), void (*ok)(void)) {
    M.items[M.count].text = text;
    M.items[M.count].ok = ok;
    M.count++;
}

static void menu_base_update(void) {
    if (zt_in.up) M.current--;
    if (zt_in.down) M.current++;
    M.current = (int)flimit((float)M.current, 0, (float)(M.count - 1));
    MenuItem *it = &M.items[M.current];
    if (zt_in.left && it->left) it->left();
    if (zt_in.right && it->right) it->right();
    if (zt_in.ok && it->ok) it->ok();
}

static void menu_base_draw(void) {
    gx_save();
    if (M.clear_alpha > 0) {
        gx_alpha(M.clear_alpha);
        gx_fill(0, 0, CANVAS_W, CANVAS_H, 0x000000);
    }
    gx_alpha(M.alpha);
    gx_scale(M.scale, M.scale);
    float xs = M.width / 2, ys = M.y;
    for (int i = 0; i < M.count; i++) {
        MenuItem *it = &M.items[i];
        if (it->y) ys = it->y;
        if (it->alpha) gx_alpha(it->alpha * M.alpha);
        ui_text(i == M.current ? FONT_36_ORANGE : FONT_36_WHITE, it->text(), xs, ys, ALIGN_CENTER);
        ys += ui_font_height(FONT_36_WHITE) * 1.5f;
        gx_alpha(1);
    }
    gx_restore();
}

/* ---------------------------------------------------------------- title */

void menu_open_title(void) {
    menu_reset(MENU_TITLE, 0.75f);
    add_item(t_new_game, ok_new_game);
    add_item(t_settings, ok_settings);
    add_item(t_stats, ok_stats);
    add_item(t_load_text, ok_load_text);
    M.items[0].y = 740;
    M.items[0].alpha = 0.9f;
    M.items[1].y = CANVAS_H / M.scale - 140;
    M.items[1].alpha = 0.4f;
    M.items[2].y = CANVAS_H / M.scale - 90;
    M.items[2].alpha = 0.4f;
    M.width = CANVAS_W / M.scale;
    M.player_y = 400;
    M.scroll = zg.yScroll2;
    modal_state = MODAL_NONE;
}

static void title_update(void) {
    if (zg.has_transition) return;
    menu_base_update();
    if (M.kind != MENU_TITLE) return;
    if (zt_in.info) menu_open_about(M.scroll);
}

static void title_draw(void) {
    float t = M.transition;
    float a = M.alpha + sinf(t * ZT_PI) * (1 - frand() * 0.5f);
    if (a >= 0 && a <= 1) gx_alpha(a);          /* canvas ignores out-of-range alpha values */

    M.scroll += zt_tick * 10;
    M.scroll = fmodf(M.scroll, (float)STARS_SRC_H);
    game_draw_stars(M.scroll);

    gx_save();
    gx_scale(0.75f, 0.75f);
    gx_spr(&SPR_PHOBOSLAB, (M.width - SPR_PHOBOSLAB.w) / 2, 80 - ease_in_out_quad(t, 0, 2800, 1));
    gx_image(TEX_LOGO, (M.width - TEX_LOGO_W) / 2, 150 - ease_in_out_quad(t, 0, 1000, 1));
    gx_alpha(sinf(t * ZT_PI));
    gx_lighter(1);
    gx_image(TEX_GL_STRIPE, 0, 4000 - t * 4200);
    gx_restore();

    float i0y = CANVAS_H / M.scale - 270;
    if (i0y > 740) i0y = 740;
    M.items[0].y = ease_in_out_quad(t, i0y, 1600, 1);
    M.items[1].y = ease_in_out_quad(t, CANVAS_H / M.scale - 200, 1900, 1);
    M.items[2].y = ease_in_out_quad(t, CANVAS_H / M.scale - 140, 2100, 1);
    M.items[3].y = ease_in_out_quad(t, CANVAS_H / M.scale - 80, 2200, 1);

    float px = (CANVAS_W - TEX_TSHIP_W) / 2.0f;
    gx_image(TEX_TSHIP, px - 1, M.player_y - 7);
    for (int i = 0; i < 10; i++) {
        float ey = fmodf(i * 139 + (float)zt_time * 53 * (1 + i / 30.0f), 400);
        float al = (ey < 20 ? (ey / 20) * 0.7f : fmaxf(0, 0.7f - ey / 400)) * 0.3f;
        gx_alpha(al);
        gx_spr(&SPR_EXHAUST, px - 1 + sinf((float)zt_time + ey / 30) * 0.7f, M.player_y + ey);
    }
    gx_alpha(1);

    menu_base_draw();

    gx_lighter(1);
    gx_alpha(frand());
    if (t > 0.55f && t < 0.62f) gx_image(TEX_GL_GRAPH, CANVAS_W - TEX_GL_GRAPH_W - frand() * 10, 10);
    if ((t > 0.6f && t < 0.7f) || (t > 0.3f && t < 0.33f)) gx_image(TEX_GL_CODE, 0 + frand() * 20, 0);
    if (t > 0.65f && t < 0.75f) gx_image(TEX_GL_LOG, 20 + frand() * 20, 400);
    if (t > 0.8f && t < 0.85f) gx_image(TEX_GL_JPEG, CANVAS_W - TEX_GL_JPEG_W, 300 + frand() * 100);
    gx_alpha(fmaxf(0.4f - t, 0));
    gx_spr(&SPR_INFORMATION, CANVAS_W - 48, CANVAS_H - 48);
    gx_alpha(1);
    gx_lighter(0);
}

/* ---------------------------------------------------------------- settings / pause */

void menu_open_settings(void) {
    int in_game = zg.mode == MODE_GAME;
    menu_reset(MENU_SETTINGS, 0.8f);
    M.clear_alpha = 0.8f;
    add_item(t_sound, NULL);
    add_item(t_music, NULL);
    M.items[0].left = sound_left;
    M.items[0].right = sound_right;
    M.items[1].left = music_left;
    M.items[1].right = music_right;
    if (in_game) add_item(t_resume, ok_resume);
    add_item(t_back, ok_back);
    M.y = CANVAS_H / 3;
    M.items[0].y = 190 / M.scale;
    M.items[1].y = 290 / M.scale;
    float low = CANVAS_H - 220;
    M.items[2].y = (low > 490 ? low : 490) / M.scale;
    M.current = 2;                        /* like hovering the first button; Enter resumes / goes back */
    sound_volume = stats_sound_volume();
    music_volume = stats_music_volume();
}

static void settings_draw(void) {
    M.width = CANVAS_W / M.scale;
    menu_base_draw();

    gx_save();
    gx_scale(0.85f, 0.85f);
    gx_alpha(0.5f);
    ui_text(FONT_36_WHITE, zg.mode != MODE_GAME ? "SETTINGS" : "GAME PAUSED", CANVAS_W / 2 / 0.85f, 60 / 0.85f, ALIGN_CENTER);
    gx_restore();

    gx_save();
    gx_scale(0.5f, 0.5f);
    gx_alpha(0.5f);
    ui_text(FONT_36_WHITE, "SOUND", CANVAS_W / 2 / 0.5f, 160 / 0.5f, ALIGN_CENTER);
    ui_text(FONT_36_WHITE, "MUSIC", CANVAS_W / 2 / 0.5f, 260 / 0.5f, ALIGN_CENTER);
    gx_restore();

    float xs = M.width / 2;
    gx_save();
    gx_alpha(0.5f);
    gx_scale(M.scale, M.scale);
    ui_text(FONT_36_WHITE, "-                +", xs, M.items[0].y, ALIGN_CENTER);
    ui_text(FONT_36_WHITE, "-                +", xs, M.items[1].y, ALIGN_CENTER);
    gx_restore();
}

/* ---------------------------------------------------------------- credits and messages */

void menu_open_about(float scroll) {
    menu_reset(MENU_ABOUT, 0.75f);
    add_item(t_back, ok_back);
    M.current = 0;
    M.width = CANVAS_W / M.scale;
    M.items[0].y = (CANVAS_H - 130) / M.scale;
    M.scroll = scroll;
}

void menu_open_message(const char *title, const char *text) {
    menu_reset(MENU_MESSAGE, 0.75f);
    add_item(t_back, ok_back);
    M.width = CANVAS_W / M.scale;
    M.items[0].y = (CANVAS_H - 130) / M.scale;
    snprintf(M.msg_title, sizeof(M.msg_title), "%s", title);
    snprintf(M.msg_text, sizeof(M.msg_text), "%s", text);
}

static void stars_background(void) {
    M.scroll += zt_tick * 10;
    M.scroll = fmodf(M.scroll, (float)STARS_SRC_H);
    game_draw_stars(M.scroll);
}

static void about_draw(void) {
    stars_background();
    gx_image(TEX_CREDITS, 32, 32);
    menu_base_draw();
}

static void message_draw(void) {
    stars_background();
    ui_text(FONT_36_WHITE, M.msg_title, 32, 32, ALIGN_LEFT);
    ui_text(FONT_18_WHITE, M.msg_text, 32, 110, ALIGN_LEFT);
    menu_base_draw();
}

/* ---------------------------------------------------------------- game over */

void menu_open_game_over(void) {
    menu_reset(MENU_GAMEOVER, 0.75f);
    add_item(t_back, ok_back);
    M.y = (CANVAS_H - 130) / M.scale;
    M.width = CANVAS_W / M.scale;
}

static float round_to_magnitude(float n) {
    if (n <= 0) return 1;
    float mag = powf(10, floorf(log10f(n)));
    return ceilf(n * 10 / mag) * mag / 10;
}

/* StatsView.drawGraph: the trend of the last games */
static void stats_view_draw(float x, float y) {
    int total = stats_game_count();
    int first = total > 30 ? total - 30 : 0;
    int n = total - first;
    if (!n) return;
    const float width = 432, height = CANVAS_H - 500, sc = 0.4f;
    float maxScore = 0;
    for (int i = 0; i < n; i++) {
        float s = (float)stats_game(first + i)->score;
        if (s > maxScore) maxScore = s;
    }
    float scoreScale = round_to_magnitude(maxScore);
    float rw = width - 32;

    for (int i = 0; i < 3; i++) {
        float v = 1.0f / 2 * i;
        char label[16];
        snprintf(label, sizeof(label), "%d", (int)fround(v * scoreScale));
        float xp = x + width, yp = y + height - height * v;
        gx_save();
        gx_scale(sc, sc);
        gx_alpha(0.8f);
        ui_text(FONT_36_WHITE, label, xp / sc, (yp - ui_font_height(FONT_36_WHITE) * sc / 2) / sc, ALIGN_RIGHT);
        gx_restore();
        gx_save();
        gx_alpha(0.1f);
        gx_line(x, yp, x + rw, yp, 1, 0xFFFFFF);
        gx_restore();
    }

    float shown = n * timer_delta(&M.timer);
    float maxi = shown < n ? shown : (float)n;
    int have_last = 0;
    float lx = 0, ly = 0;
    for (int i = 0; i < maxi; i++) {
        float v = n > 1 ? 1.0f / (n - 1) * i : 0;
        float rx = x + (rw - 3) * v;
        float ry = y + height - (height - 3) * (stats_game(first + i)->score / scoreScale);
        gx_fill(rx, ry, 3, 3, 0x4DFED2);
        if (have_last) {
            gx_save();
            gx_alpha(0.2f);
            gx_line(lx + 1.5f, ly + 1.5f, rx + 1.5f, ry + 1.5f, 2, 0xFFFFFF);
            gx_restore();
        }
        have_last = 1;
        lx = rx;
        ly = ry;
    }
}

static void zero_fill(char *out, size_t n, int v, int digits) { snprintf(out, n, "%0*d", digits, v); }

static void game_over_update(void) {
    if (timer_delta(&M.timer) > 1.5f) menu_base_update();
}

static void game_over_draw(void) {
    menu_base_draw();

    float ys = 25;
    float acc = zg.hits ? (float)zg.hits / (zg.hits + zg.misses) * 100.0f : 0;
    char buf[32];

    if (zg.isPersonalBest) {
        gx_save();
        gx_scale(0.5f, 0.5f);
        gx_spr(&SPR_PERSONAL_BEST, 24 / 0.5f, 275 / 0.5f);
        ui_text(FONT_36_WHITE, "NEW PERSONAL BEST", 60 / 0.5f, 280 / 0.5f, ALIGN_LEFT);
        gx_restore();
    }

    float ss = 0.5f;
    gx_save();
    gx_scale(ss, ss);
    gx_alpha(0.5f);
    ui_text(FONT_36_WHITE, "FINAL SCORE", 24 / ss, (ys + 0) / ss, ALIGN_LEFT);
    ui_text(FONT_36_WHITE, "YOU REACHED", 252 / ss, (ys + 0) / ss, ALIGN_LEFT);
    ui_text(FONT_36_WHITE, "ACCURACY", 24 / ss, (ys + 140) / ss, ALIGN_LEFT);
    ui_text(FONT_36_WHITE, "LONGEST STREAK", 252 / ss, (ys + 140) / ss, ALIGN_LEFT);
    gx_restore();

    zero_fill(buf, sizeof(buf), zg.score, 6);
    ui_text(FONT_36_BLUE, buf, 24, ys + 25, ALIGN_LEFT);
    char wave[48];
    zero_fill(buf, sizeof(buf), zg.wave, 3);
    snprintf(wave, sizeof(wave), "WAVE %s", buf);
    ui_text(FONT_36_BLUE, wave, 252, ys + 25, ALIGN_LEFT);
    gx_fill(24, ys + 70, 432, 2, 0x4DFED2);

    float r = floorf(acc * 10 + 0.5f) / 10;
    if (r == floorf(r)) snprintf(buf, sizeof(buf), "%d%%", (int)r);
    else snprintf(buf, sizeof(buf), "%.1f%%", r);
    ui_text(FONT_36_BLUE, buf, 24, ys + 165, ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "%d", zg.longestStreak);
    ui_text(FONT_36_BLUE, buf, 252, ys + 165, ALIGN_LEFT);
    gx_fill(24, ys + 210, 432, 2, 0x4DFED2);

    stats_view_draw(24, ys + 300);
}

/* ---------------------------------------------------------------- "my stats" card (DetailedStats) */

/*
 * The card is an HTML overlay on zty.pe: it is laid out in browser pixels, so it is drawn in pixels of
 * the 640x480 frame (page space is 1.5x that) and not scaled down with the page.
 */
#define FRAME_H 480.0f

void menu_open_stats(void) {
    modal_state = MODAL_OPEN;
    timer_init(&modal_timer, 0);
}

static void modal_update(void) {
    float age = timer_delta(&modal_timer);
    if (modal_state == MODAL_OPEN) {
        if (age > 0.4f && (zt_in.ok || zt_in.menu || zt_in.back || zt_in.enter || zt_in.click)) {
            modal_state = MODAL_CLOSING;
            timer_init(&modal_timer, 0);
        }
    } else if (age > 0.3f) {
        modal_state = MODAL_NONE;
    }
}

static float ease_css(float t) {                 /* transition: width .3s ease-in-out */
    t = flimit(t, 0, 1);
    return t * t * (3 - 2 * t);
}

static void stats_card_content(float width) {
    const float height = FRAME_H - 64 - 128;     /* canvas.height - 128 */
    const u32 ink = 0x555555;
    int n = stats_game_count();
    if (!n) {
        sans_text(28, "NO STATS FOUND - PLAY SOME MORE", width / 2, height / 2, ALIGN_CENTER, ink);
        return;
    }
    float maxScore = 0;
    for (int i = 0; i < n; i++)
        if (stats_game(i)->score > maxScore) maxScore = (float)stats_game(i)->score;
    float scoreScale = round_to_magnitude(maxScore);
    const float x = 32, y = 64, rw = width - 84;
    char buf[64];

    sans_text(28, "ZTYPE", 32, 38, ALIGN_LEFT, ink);
    snprintf(buf, sizeof(buf), "SCORES OVER %d GAMES", n);
    sans_text(28, buf, 140, 38, ALIGN_LEFT, 0xCCCCCC);

    for (int i = 0; i < 8; i++) {
        float v = 1.0f / 7 * i;
        float xp = x + rw + 32, yp = y + height - height * v;
        snprintf(buf, sizeof(buf), "%d", (int)fround(v * scoreScale));
        sans_text(10, buf, xp, yp + 3, ALIGN_RIGHT, ink);
        gx_line(x, yp, x + rw, yp, 1, 0xF0F0F0);
    }

    float lx = 0, ly = 0;
    for (int i = 0; i < n; i++) {
        float v = n > 1 ? 1.0f / (n - 1) * i : 0;
        float rx = x + (rw - 3) * v;
        float ry = y + height - (height - 3) * (stats_game(i)->score / scoreScale);
        gx_fill(rx, ry, 3, 3, 0x4DFED2);
        if (i) {
            gx_save();
            gx_alpha(0.2f);
            gx_line(lx + 1.5f, ly + 1.5f, rx + 1.5f, ry + 1.5f, 1, 0x000000);
            gx_restore();
        }
        lx = rx;
        ly = ry;
    }
}

void menu_draw_overlay(void) {
    if (modal_state == MODAL_NONE) return;
    float W = gx_page_width() / 1.5f;
    float age = timer_delta(&modal_timer);
    float grow = modal_state == MODAL_OPEN ? ease_css(age / 0.3f) : 1 - ease_css(age / 0.3f);
    float full = W * 0.98f;
    float w = 2 + (full - 2) * grow;
    float x = (W - w) / 2;

    gx_save();
    gx_scale(1.5f, 1.5f);
    gx_fill(x, 0, w, FRAME_H, 0xFFFFFF);
    if (modal_state == MODAL_OPEN && age > 0.4f) {
        gx_save();
        gx_translate(x, 0);
        stats_card_content(w);

        /* #ztype-stats-close: a 48px "x" in a 64px box */
        gx_line(w - 43, 21, w - 21, 43, 4.2f, 0x555555);
        gx_line(w - 43, 43, w - 21, 21, 4.2f, 0x555555);

        /* #ztype-stats-foot: the share links (they only decorate the card here) */
        float fx = 32, top = 435, base = top + 18.6f, space = sans_text_width(14, " ");
        gx_spr(&SPR_FB_BUTTON, fx, top);
        fx += SPR_FB_BUTTON.w + 4 + space;
        sans_text(14, "share on facebook", fx, base, ALIGN_LEFT, 0x555555);
        fx += sans_text_width(14, "share on facebook") + 32 + space;
        gx_spr(&SPR_TWITTER_BUTTON, fx, top);
        fx += SPR_TWITTER_BUTTON.w + 4 + space;
        sans_text(14, "share on twitter", fx, base, ALIGN_LEFT, 0x555555);
        gx_restore();
    }
    gx_restore();
}

/* ---------------------------------------------------------------- dispatch */

MenuKind menu_kind(void) { return M.kind; }
int menu_active(void) { return M.kind != MENU_NONE; }
int menu_modal_open(void) { return modal_state != MODAL_NONE; }

void menu_close(void) {
    M.kind = MENU_NONE;
    M.count = 0;
}

void menu_update(void) {
    if (modal_state != MODAL_NONE) {
        modal_update();
        return;
    }
    switch (M.kind) {
    case MENU_TITLE: title_update(); break;
    case MENU_GAMEOVER: game_over_update(); break;
    case MENU_NONE: break;
    default: menu_base_update(); break;
    }
}

void menu_draw(void) {
    switch (M.kind) {
    case MENU_TITLE: title_draw(); break;
    case MENU_SETTINGS: settings_draw(); break;
    case MENU_ABOUT: about_draw(); break;
    case MENU_MESSAGE: message_draw(); break;
    case MENU_GAMEOVER: game_over_draw(); break;
    default: break;
    }
}

float menu_scroll(int *has_scroll) {
    *has_scroll = M.kind == MENU_TITLE || M.kind == MENU_ABOUT;
    return M.scroll;
}

void menu_set_transition(float transition, float alpha, float *player_y) {
    if (M.kind != MENU_TITLE) return;
    M.transition = transition;
    M.alpha = alpha;
    if (player_y) M.player_y = *player_y;
}

float menu_title_player_y(void) { return M.player_y; }

float menu_game_over_age(void) {
    return M.kind == MENU_GAMEOVER ? timer_delta(&M.timer) : 0;
}
