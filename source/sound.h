#ifndef SOUND_H
#define SOUND_H

#include "assets.h"

void sound_init(void);
void sound_play(int id);
void sound_toggle_mute(void);
int sound_muted(void);

#endif
