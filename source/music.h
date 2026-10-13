#ifndef MUSIC_H
#define MUSIC_H
/* Streams the original soundtrack (.ogg) from sd:/apps/typeshot/music/ when present. */
int music_available(void);
void music_init(void);
void music_start(void);
void music_stop(float fade);
void music_update(float dt, int vol);
int music_active(void);
#endif
