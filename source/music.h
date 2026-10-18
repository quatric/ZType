#ifndef MUSIC_H
#define MUSIC_H

/* ig.music: the original soundtracks, endure.ogg and orientation.ogg */
void music_init(void);
void music_next(void);                  /* a random track, repeating; ig.music.next() */
void music_fade_out(float seconds);     /* ig.music.fadeOut() */
void music_set_volume(float v);         /* 0..1, ig.music.volume */
void music_update(float dt);            /* once a frame: starts the voice, runs the fade */

#endif
