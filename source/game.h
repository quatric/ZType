#ifndef GAME_H
#define GAME_H

enum { K_UP = 1, K_DOWN, K_LEFT, K_RIGHT, K_OK, K_MENU, K_BACK, K_EMP, K_ABOUT };

void game_init(int sdOK);
void game_update(float dt);
void game_draw(void);
void game_char(int c);
void game_key(int k);
void game_set_keyboard(int present);
int game_wants_quit(void);

#endif
