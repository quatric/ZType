#include <gccore.h>
#include <asndlib.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>
#include <tremor/ivorbisfile.h>
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
#define MUSIC_GAIN 120      /* the synthesized fallback loop */
#define OGG_GAIN 340        /* the streamed soundtrack */
#define SFX_GAIN 480

/* The soundtrack of the website: endure.ogg and orientation.ogg in random order, looping.
 * They are streamed from the SD card; without them the synthesized loop is played instead. */
#define MUSIC_DIR "sd:/apps/typeshot/music/"
#define STREAM_FRAMES 4096
#define STREAM_BUFS 6
#define STREAM_BYTES (STREAM_FRAMES * 2 * 2)
#define STREAM_FILEBUF (32 * 1024)

static const void *sfx_ptrs[SFX_MAX];
static u32 sfx_sizes[SFX_MAX];

static s16 *music;
static int musicLen;
static volatile int musicReady;
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
    int n = (int)(16.f * RATE), len;
    s16 *m = alloc_aligned(n, &len);
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
        m[i] = (s16)(v * 14000);
    }
    m[n - 1] = 0;
    DCFlushRange(m, len);
    musicLen = len;
    music = m;
    musicReady = 1;
}

/* ---------------------------------------------------------------- music streaming */

enum { MUS_NONE, MUS_OGG, MUS_SYNTH };

static const char *TRACKS[2] = { MUSIC_DIR "endure.ogg", MUSIC_DIR "orientation.ogg" };

/* main thread state */
static int musKind;                 /* what the running game plays */
static int musPlaying;              /* the ASND voice is running */
static int musFading;
static float fadeLeft, fadeTotal;
static int lastVol = -1;

/* decoder thread: fills a ring of buffers that the voice callback hands to ASND */
static s16 *sbuf[STREAM_BUFS], *silence;
static volatile int sst[STREAM_BUFS];            /* 0 empty, 1 decoded, 2 queued on the voice */
static int sbytes[STREAM_BUFS];
static int fillIdx, cbIdx, fifo[3], nfifo;       /* nfifo: buffers the voice holds (-1 = silence) */
static volatile int want, idle = 1, openFailed, synthReq;
static int curTrack, vfOpen, srate, schan;
static OggVorbis_File vf;
static lwp_t thread;
static int threadStarted;

static int track_exists(void) {
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(TRACKS[i], "rb");
        if (f) { fclose(f); return 1; }
    }
    return 0;
}

static int open_track(void) {
    for (int k = 0; k < 2; k++) {
        FILE *f = fopen(TRACKS[curTrack], "rb");
        if (f) {
            setvbuf(f, NULL, _IOFBF, STREAM_FILEBUF);
            if (ov_open(f, &vf, NULL, 0) >= 0) {
                vorbis_info *vi = ov_info(&vf, -1);
                if (vi && vi->channels >= 1 && vi->channels <= 2) {
                    srate = (int)vi->rate;
                    schan = vi->channels;
                    vfOpen = 1;
                    return 1;
                }
                ov_clear(&vf);
            } else {
                fclose(f);
            }
        }
        curTrack ^= 1;
    }
    return 0;
}

static void *stream_worker(void *arg) {
    (void)arg;
    for (;;) {
        if (synthReq) {
            if (!musicReady) mkMusic();
            synthReq = 0;
        }
        if (!want) {
            if (vfOpen) { ov_clear(&vf); vfOpen = 0; }
            for (int i = 0; i < STREAM_BUFS; i++) sst[i] = 0;
            fillIdx = 0;
            idle = 1;
            usleep(30000);
            continue;
        }
        idle = 0;
        if (!vfOpen && !open_track()) { openFailed = 1; usleep(100000); continue; }
        int s = fillIdx;
        if (sst[s]) { usleep(4000); continue; }
        int bytes = STREAM_FRAMES * schan * 2, got = 0;
        while (got < bytes && want) {
            int sec;
            long r = ov_read(&vf, (char *)sbuf[s] + got, bytes - got, &sec);
            if (r <= 0) {
                /* end of the track: ig.music picks the next one at random */
                ov_clear(&vf);
                vfOpen = 0;
                curTrack = (int)((gettime() >> 3) & 1);
                if (!open_track()) { openFailed = 1; break; }
            } else {
                got += r;
            }
        }
        if (!want) continue;
        if (got < bytes) memset((char *)sbuf[s] + got, 0, bytes - got);
        sbytes[s] = bytes;
        DCFlushRange(sbuf[s], bytes);
        sst[s] = 1;
        fillIdx = (s + 1) % STREAM_BUFS;
    }
    return NULL;
}

/* ASND calls this when the voice has taken over its queued buffer: refill the queue */
static void stream_cb(s32 voice) {
    if (!musPlaying) return;
    if (nfifo > 0) {
        int done = fifo[0];
        fifo[0] = fifo[1];
        nfifo--;
        if (done >= 0) sst[done] = 0;
    }
    int n = cbIdx;
    if (sst[n] == 1) {
        ASND_AddVoice(voice, sbuf[n], sbytes[n]);
        sst[n] = 2;
        fifo[nfifo++] = n;
        cbIdx = (n + 1) % STREAM_BUFS;
    } else {
        ASND_AddVoice(voice, silence, 4096);      /* the decoder is late: bridge the gap */
        fifo[nfifo++] = -1;
    }
}

static void synth_cb(s32 voice) {
    ASND_AddVoice(voice, music, musicLen);
}

static int music_level(void) {
    float f = musFading ? fadeLeft / fadeTotal : 1.f;
    return level(musKind == MUS_OGG ? OGG_GAIN : MUSIC_GAIN, musicVolume * f);
}

static void music_apply(void) {
    if (!musPlaying) return;
    int v = music_level();
    if (v != lastVol) {
        ASND_ChangeVolumeVoice(MUSIC_VOICE, v, v);
        lastVol = v;
    }
}

static void music_halt(void) {
    if (musPlaying) ASND_StopVoice(MUSIC_VOICE);
    musPlaying = 0;
    musFading = 0;
    musKind = MUS_NONE;
    want = 0;
}

static void synth_begin(void) {
    musKind = MUS_SYNTH;
    synthReq = 1;
}

void sound_music_start(void) {
    if (!threadStarted) {
        for (int i = 0; i < STREAM_BUFS; i++) {
            sbuf[i] = memalign(32, STREAM_BYTES);
            memset(sbuf[i], 0, STREAM_BYTES);
        }
        silence = memalign(32, 4096);
        memset(silence, 0, 4096);
        DCFlushRange(silence, 4096);
        void *stack = memalign(32, 32768);
        LWP_CreateThread(&thread, stream_worker, NULL, stack, 32768, 70);
        threadStarted = 1;
    }
    /* a new game restarts the music like ig.music.next() */
    musPlaying = 0;
    ASND_StopVoice(MUSIC_VOICE);
    musFading = 0;
    lastVol = -1;
    want = 0;
    for (int n = 0; !idle && n < 150; n++) usleep(2000);   /* let the decoder drop the old track */
    nfifo = 0;
    cbIdx = 0;
    openFailed = 0;
    if (track_exists()) {
        musKind = MUS_OGG;
        curTrack = rand() & 1;
        idle = 0;
        want = 1;
    } else {
        synth_begin();
    }
}

void sound_music_stop(float fade) {
    if (musKind == MUS_NONE) return;
    if (fade <= 0.f || !musPlaying) { music_halt(); return; }
    musFading = 1;
    fadeLeft = fadeTotal = fade;
}

void sound_update(float dt) {
    if (musKind == MUS_NONE) return;
    if (musKind == MUS_OGG && openFailed) {
        /* the files are there but cannot be decoded: use the synthesized loop */
        want = 0;
        musPlaying = 0;
        ASND_StopVoice(MUSIC_VOICE);
        synth_begin();
        return;
    }
    if (!musPlaying) {
        int v = music_level();
        if (musKind == MUS_OGG) {
            /* start once two buffers are decoded */
            if (sst[0] == 1 && sst[1] == 1 && schan) {
                nfifo = 0;
                cbIdx = 0;
                musPlaying = 1;
                ASND_SetVoice(MUSIC_VOICE, schan == 2 ? VOICE_STEREO_16BIT : VOICE_MONO_16BIT, srate, 0,
                              sbuf[0], sbytes[0], v, v, stream_cb);
                sst[0] = 2; fifo[nfifo++] = 0;
                ASND_AddVoice(MUSIC_VOICE, sbuf[1], sbytes[1]);
                sst[1] = 2; fifo[nfifo++] = 1;
                cbIdx = 2;
                lastVol = v;
            }
        } else if (musicReady) {
            musPlaying = 1;
            ASND_SetVoice(MUSIC_VOICE, VOICE_MONO_16BIT, RATE, 0, music, musicLen, v, v, synth_cb);
            ASND_AddVoice(MUSIC_VOICE, music, musicLen);
            lastVol = v;
        }
        return;
    }
    if (musFading) {
        fadeLeft -= dt;
        if (fadeLeft <= 0.f) { music_halt(); return; }
    }
    music_apply();
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

}

void sound_play(int id) {
    if (sfxVolume <= 0 || id < 0 || id >= SFX_MAX || !sfx_ptrs[id]) return;
    int v = nextVoice;
    nextVoice = (nextVoice >= 15) ? 1 : (nextVoice + 1);
    ASND_SetVoice(v, VOICE_MONO_16BIT, RATE, 0, (void*)sfx_ptrs[id], sfx_sizes[id],
                  level(SFX_GAIN, sfxVolume), level(SFX_GAIN, sfxVolume), NULL);
}

void sound_set_volumes(float sound, float vol_music) {
    sfxVolume = sound;
    musicVolume = vol_music;
    music_apply();
}
