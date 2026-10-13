#ifndef SOUND_H
#define SOUND_H
enum { SFX_SHOOT, SFX_MISS, SFX_EXPLODE, SFX_BREACH, SFX_LEVEL, SFX_OVER, SFX_COUNT };
void sound_init(void);
void sound_play(int sfx);
void sound_toggle_mute(void);
int sound_muted(void);
#endif
