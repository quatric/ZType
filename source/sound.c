#include <gccore.h>
#include <asndlib.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
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

#define RATE 32000
#define MUSIC_VOICE 0
/* ASND voice volumes (0..255) at a setting of 100%; ZType starts at 50% */
#define MUSIC_GAIN 120
#define SFX_GAIN 480

static const void *sfx_ptrs[SFX_MAX];
static u32 sfx_sizes[SFX_MAX];

static s16 *music;
static int musicLen;
static float sfxVolume = 0.5f, musicVolume = 0.5f;

static int level(float gain, float v) {
    int l = (int)(gain * v);
    return l > 255 ? 255 : l < 0 ? 0 : l;
}
static int nextVoice = 1;

static float midi(int n) { return 440.f * powf(2.f, (n - 69) / 12.f); }
static float tri(float ph) { float p = ph - floorf(ph); return 4.f * fabsf(p - 0.5f) - 1.f; }
static float sq(float ph) { return (ph - floorf(ph)) < 0.5f ? 1.f : -1.f; }
static unsigned nz = 1;
static float noise(void) { nz = nz * 1664525u + 1013904223u; return ((nz >> 16) & 0x7fff) / 16384.f - 1.f; }

static s16 *alloc_aligned(int n, int *outLen) {
    int bytes = (n * 2 + 31) & ~31;
    s16 *b = memalign(32, bytes);
    memset(b, 0, bytes);
    *outLen = bytes;
    return b;
}

static void mkMusic(void) {
    /* 8 bars at 120 bpm (2s per bar) = 16s loop */
    static const int root[8] = {45, 41, 48, 43, 45, 41, 43, 40};
    static const int chord[8][3] = {{0,3,7},{0,4,7},{0,4,7},{0,4,7},{0,3,7},{0,4,7},{0,4,7},{0,4,7}};
    float beat = 0.25f;
    int n = (int)(16.f * RATE);
    music = alloc_aligned(n, &musicLen);
    float pb = 0, pa = 0;
    for (int i = 0; i < n; i++) {
        float t = i / (float)RATE;
        int bar = (int)(t / 2.f) % 8;
        float inBar = t - bar * 2.f;
        int step = (int)(inBar / beat);
        float inStep = (inBar - step * beat) / beat;
        pb += midi(root[bar]) / RATE;
        float bass = tri(pb) * (1.f - inStep * 0.6f) * 0.55f;
        int note = root[bar] + 24 + chord[bar][(step * 2 + (step / 4)) % 3];
        pa += midi(note) / RATE;
        float arp = sq(pa) * (1.f - inStep) * 0.18f;
        float hat = (step & 1) ? noise() * (1.f - inStep) * (1.f - inStep) * 0.12f : 0.f;
        float kick = 0;
        if (!(step & 1) && inStep < 0.5f) {
            kick = sinf(2 * 3.14159f * (60.f + 90.f * (0.5f - inStep)) * (inStep * beat)) * (1.f - inStep * 2.f) * 0.5f;
        }
        float v = bass + arp + hat + kick;
        if (v > 1.f) v = 1.f;
        if (v < -1.f) v = -1.f;
        music[i] = (s16)(v * 14000);
    }
    music[n - 1] = 0;
    DCFlushRange(music, musicLen);
}

static void musicLoop(s32 voice) {
    ASND_AddVoice(voice, music, musicLen);
}

static void musicStart(void) {
    ASND_SetVoice(MUSIC_VOICE, VOICE_MONO_16BIT, RATE, 0, music, musicLen,
                  level(MUSIC_GAIN, musicVolume), level(MUSIC_GAIN, musicVolume), musicLoop);
    ASND_AddVoice(MUSIC_VOICE, music, musicLen);
}

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

    mkMusic();
    musicStart();
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
    musicVolume = music;
    ASND_ChangeVolumeVoice(MUSIC_VOICE, level(MUSIC_GAIN, music), level(MUSIC_GAIN, music));
}
