#ifndef MENUS_H
#define MENUS_H

#include "zt.h"

/* The menus of ZType: title, settings/pause, credits and game over, plus the "my stats" card */
typedef enum { MENU_NONE, MENU_TITLE, MENU_SETTINGS, MENU_ABOUT, MENU_GAMEOVER, MENU_MESSAGE, MENU_STATS } MenuKind;

MenuKind menu_kind(void);
int menu_active(void);

void menu_open_title(void);
void menu_open_settings(void);
void menu_open_about(float scroll);
void menu_open_game_over(void);
void menu_open_message(const char *title, const char *text);
void menu_open_stats(void);
void menu_close(void);

void menu_update(void);
void menu_draw(void);

/* coupling with the game: the title scrolls the stars and animates out while a game starts */
float menu_scroll(int *has_scroll);
void menu_set_transition(float transition, float alpha, float *player_y);
float menu_title_player_y(void);
float menu_game_over_age(void);

/* the "my stats" card covers the whole page, so it is drawn in page space after the game */
int menu_modal_open(void);
void menu_draw_overlay(void);

#endif
