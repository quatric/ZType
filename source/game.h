#ifndef GAME_H
#define GAME_H

#include "zt.h"

typedef enum { MODE_TITLE, MODE_GAME, MODE_GAME_OVER } GameMode;

typedef struct Entity Entity;

/* the ZType game object (the fields the menus need to see) */
typedef struct {
    GameMode mode;
    int score, streak, longestStreak, hits, misses, multiplier, kills, emps;
    int personalBest, isPersonalBest;
    int wave;                        /* ig.game.wave.wave */
    float yScroll2;                  /* stars scroll, shared with the title menu */
    float yScroll;                   /* grid scroll */
    int has_transition;              /* gameTransitionTimer is running */
    Timer transition;
    Entity *player;
    float rscreen_x, rscreen_y;      /* screen shake offset */
} ZGame;

extern ZGame zg;

void game_init(void);
void game_update(void);
void game_draw(void);

void game_set_title(void);
void game_set_game(void);
void game_set_game_over(void);
void game_show_game_over(void);

void game_key_letter(char c);        /* a printable key was typed */
void game_cancel_target(void);       /* Backspace */
void game_spawn_emp(void);           /* Enter */

/* the background that the title menu and the credits draw (stars.jpg at 0.75 scale) */
void game_draw_stars(float scroll);

/* "load your own text": replaces the built-in word lists */
int game_load_words(const char *text);   /* returns the number of usable words (0 = unchanged) */
int game_has_custom_words(void);

#ifdef ZT_TEST_SCRIPT
char game_test_next_letter(void);    /* scripted test runs: the letter an autopilot would type next */
#endif

#endif
