#ifndef SOUND_H
#define SOUND_H

#include "assets.h"

void sound_init(void);
void sound_play(int id);
void sound_set_volumes(float sound, float music);   /* 0..1 each, like ig.soundManager.volume / ig.music.volume */

/* The soundtrack: endure.ogg / orientation.ogg streamed from sd:/apps/typeshot/music/ (a
 * synthesized loop when they are missing). */
void sound_music_start(void);              /* a game starts: a random track, like ig.music.next() */
void sound_music_stop(float fade);         /* game over: fade out over `fade` seconds, like ig.music.fadeOut() */
void sound_update(float dt);               /* once per frame: starts the stream and runs the fade */

#endif
