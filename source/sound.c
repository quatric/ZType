#include <gccore.h>
#include <asndlib.h>
#include <malloc.h>
#include <string.h>
#include "sound.h"

#include "sfx_plasma_bin.h"
#include "sfx_hit_bin.h"
#include "sfx_click_bin.h"
#include "sfx_target_bin.h"
#include "sfx_explode_s_bin.h"
#include "sfx_explode_m_bin.h"
#include "sfx_explode_l_bin.h"
#include "sfx_explode_p_bin.h"
#include "sfx_emp_bin.h"
#include "sfx_spawn_bin.h"
#include "sfx_multi_2_bin.h"
#include "sfx_multi_3_bin.h"
#include "sfx_cancel_bin.h"

#include "music.h"

#define RATE 32000
/* the voice volume (0..255) at a setting of 100%: the samples as they are, like the website */
#define SFX_GAIN 255

static const void *sfx_ptrs[SFX_MAX];
static u32 sfx_sizes[SFX_MAX];

static float sfxVolume = 0.5f;

static int level(float gain, float v) {
    int l = (int)(gain * v + 0.5f);
    return l > 255 ? 255 : l < 0 ? 0 : l;
}
static int nextVoice = 1;

void sound_init(void) {
    AUDIO_Init(NULL);
    ASND_Init();
    ASND_Pause(0);

    sfx_ptrs[SFX_PLASMA]     = sfx_plasma_bin;     sfx_sizes[SFX_PLASMA]     = sfx_plasma_bin_size;
    sfx_ptrs[SFX_HIT]        = sfx_hit_bin;        sfx_sizes[SFX_HIT]        = sfx_hit_bin_size;
    sfx_ptrs[SFX_CLICK]      = sfx_click_bin;      sfx_sizes[SFX_CLICK]      = sfx_click_bin_size;
    sfx_ptrs[SFX_TARGET]     = sfx_target_bin;     sfx_sizes[SFX_TARGET]     = sfx_target_bin_size;
    sfx_ptrs[SFX_EXPLODE_S]  = sfx_explode_s_bin;  sfx_sizes[SFX_EXPLODE_S]  = sfx_explode_s_bin_size;
    sfx_ptrs[SFX_EXPLODE_M]  = sfx_explode_m_bin;  sfx_sizes[SFX_EXPLODE_M]  = sfx_explode_m_bin_size;
    sfx_ptrs[SFX_EXPLODE_L]  = sfx_explode_l_bin;  sfx_sizes[SFX_EXPLODE_L]  = sfx_explode_l_bin_size;
    sfx_ptrs[SFX_EXPLODE_P]  = sfx_explode_p_bin;  sfx_sizes[SFX_EXPLODE_P]  = sfx_explode_p_bin_size;
    sfx_ptrs[SFX_EMP]        = sfx_emp_bin;        sfx_sizes[SFX_EMP]        = sfx_emp_bin_size;
    sfx_ptrs[SFX_SPAWN]      = sfx_spawn_bin;      sfx_sizes[SFX_SPAWN]      = sfx_spawn_bin_size;
    sfx_ptrs[SFX_MULTI_2]    = sfx_multi_2_bin;    sfx_sizes[SFX_MULTI_2]    = sfx_multi_2_bin_size;
    sfx_ptrs[SFX_MULTI_3]    = sfx_multi_3_bin;    sfx_sizes[SFX_MULTI_3]    = sfx_multi_3_bin_size;
    sfx_ptrs[SFX_CANCEL]     = sfx_cancel_bin;     sfx_sizes[SFX_CANCEL]     = sfx_cancel_bin_size;

    for (int i = 0; i < SFX_MAX; i++) {
        if (sfx_ptrs[i]) {
            DCFlushRange((void*)sfx_ptrs[i], sfx_sizes[i]);
        }
    }

    music_init();
}

void sound_play(int id) {
    if (sfxVolume <= 0 || id < 0 || id >= SFX_MAX || !sfx_ptrs[id]) return;
    int v = nextVoice;
    nextVoice = (nextVoice >= 15) ? 1 : (nextVoice + 1);
    ASND_SetVoice(v, VOICE_MONO_16BIT, RATE, 0, (void*)sfx_ptrs[id], sfx_sizes[id],
                  level(SFX_GAIN, sfxVolume), level(SFX_GAIN, sfxVolume), NULL);
}

void sound_set_volumes(float sound, float music) {
    sfxVolume = sound;
    music_set_volume(music);
}
