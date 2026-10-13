#ifndef SOUND_H
#define SOUND_H
enum { SFX_SHOOT, SFX_MISS, SFX_EXPLODE, SFX_EXPLODE_L, SFX_BREACH, SFX_LEVEL, SFX_OVER,
       SFX_TARGET, SFX_HIT, SFX_EMP, SFX_CLICK, SFX_CANCEL, SFX_SPAWN, SFX_MULTI, SFX_COUNT };
void sound_init(void);
void sound_play(int sfx);
void sound_toggle_mute(void);
int sound_muted(void);
void sound_set_sfx_volume(float v);
void sound_set_music_volume(float v);
void sound_music_start(void);
void sound_music_stop(float fade);
void sound_update(float dt);
#endif
