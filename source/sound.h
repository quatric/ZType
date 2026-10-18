#ifndef SOUND_H
#define SOUND_H

#include "assets.h"

void sound_init(void);
void sound_play(int id);
void sound_set_volumes(float sound, float music);   /* 0..1 each, like ig.soundManager.volume / ig.music.volume */

#endif
