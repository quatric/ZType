/*
 * ig.music: the two ZType soundtracks, linked into the program as the original .ogg files and decoded
 * while they play. A worker thread decodes into a ring of buffers that the ASND voice callback queues
 * on the music voice.
 *
 * Like the website: ig.music.random and ig.music.loop are on, so every new game picks one of the two
 * tracks at random and repeats it, and the game over screen fades it out over a second.
 */
#include <gccore.h>
#include <asndlib.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <tremor/ivorbisfile.h>
#include "music.h"

#include "music_endure_ogg.h"
#include "music_orientation_ogg.h"

#define MUSIC_VOICE 0
#define FRAMES 4096
#define NBUF 4
#define BUFBYTES (FRAMES * 2 * 2)
#define SILENCE_BYTES 4096

#define TRACKS 2
static const u8 *track_data(int t) { return t == 0 ? music_endure_ogg : music_orientation_ogg; }
static u32 track_size(int t) { return t == 0 ? music_endure_ogg_size : music_orientation_ogg_size; }

/* ---------------------------------------------------------------- reading an .ogg from memory */

typedef struct { const u8 *data; u32 size, pos; } MemFile;
static MemFile mf;

static size_t mem_read(void *ptr, size_t size, size_t n, void *src) {
    MemFile *m = src;
    size_t want = size * n, left = m->size - m->pos;
    if (want > left) want = left;
    memcpy(ptr, m->data + m->pos, want);
    m->pos += want;
    return size ? want / size : 0;
}

static int mem_seek(void *src, ogg_int64_t off, int whence) {
    MemFile *m = src;
    ogg_int64_t p = whence == SEEK_SET ? off : whence == SEEK_CUR ? m->pos + off : m->size + off;
    if (p < 0 || p > m->size) return -1;
    m->pos = (u32)p;
    return 0;
}

static int mem_close(void *src) { (void)src; return 0; }
static long mem_tell(void *src) { return ((MemFile *)src)->pos; }

/* ---------------------------------------------------------------- decoding and playback */

static s16 *buf[NBUF], *silence;
static volatile int st[NBUF];            /* 0 empty, 1 decoded, 2 queued on the voice */
static int blen[NBUF];
static int fillIdx, cbIdx, fifo[3], nfifo;
static volatile int want, playing, restart;
static int track, vfOpen, rate, ch;
static OggVorbis_File vf;
static lwp_t thread;
static float volume = 0.5f, fadeLeft, fadeTotal;
static int fading, lastVol = -1;

static int open_track(void) {
    ov_callbacks cb = { mem_read, mem_seek, mem_close, mem_tell };
    mf.data = track_data(track);
    mf.size = track_size(track);
    mf.pos = 0;
    if (ov_open_callbacks(&mf, &vf, NULL, 0, cb) < 0) return 0;
    vorbis_info *vi = ov_info(&vf, -1);
    if (!vi || vi->channels < 1 || vi->channels > 2) { ov_clear(&vf); return 0; }
    rate = (int)vi->rate;
    ch = vi->channels;
    vfOpen = 1;
    return 1;
}

static void *worker(void *arg) {
    (void)arg;
    for (;;) {
        if (restart) {
            /* music_next() has stopped the voice: drop the old track and its buffers */
            if (vfOpen) { ov_clear(&vf); vfOpen = 0; }
            for (int i = 0; i < NBUF; i++) st[i] = 0;
            fillIdx = 0;
            restart = 0;
        }
        if (!want) {
            if (vfOpen) { ov_clear(&vf); vfOpen = 0; }
            usleep(20000);
            continue;
        }
        if (!vfOpen && !open_track()) { want = 0; continue; }
        int s = fillIdx;
        if (st[s]) { usleep(4000); continue; }
        int bytes = FRAMES * ch * 2, got = 0;
        while (got < bytes && want && !restart) {
            int sec;
            long r = ov_read(&vf, (char *)buf[s] + got, bytes - got, &sec);
            if (r <= 0) {
                /* ig.music.loop: the track starts over */
                if (ov_raw_seek(&vf, 0) < 0) break;
            } else {
                got += r;
            }
        }
        if (!want || restart) continue;
        if (got < bytes) memset((char *)buf[s] + got, 0, bytes - got);
        blen[s] = bytes;
        DCFlushRange(buf[s], bytes);
        st[s] = 1;
        fillIdx = (s + 1) % NBUF;
    }
    return NULL;
}

/* ASND calls this from the audio interrupt when the voice wants its next buffer */
static void voice_cb(s32 voice) {
    if (!playing) return;
    if (nfifo > 0) {
        int done = fifo[0];
        fifo[0] = fifo[1];
        fifo[1] = fifo[2];
        nfifo--;
        if (done >= 0) st[done] = 0;
    }
    int n = cbIdx;
    if (st[n] == 1) {
        ASND_AddVoice(voice, buf[n], blen[n]);
        st[n] = 2;
        fifo[nfifo++] = n;
        cbIdx = (n + 1) % NBUF;
    } else {
        ASND_AddVoice(voice, silence, SILENCE_BYTES);
        fifo[nfifo++] = -1;
    }
}

static int level(float v) {
    int l = (int)(255 * v + 0.5f);
    return l > 255 ? 255 : l < 0 ? 0 : l;
}

static void stop_voice(void) {
    playing = 0;
    ASND_StopVoice(MUSIC_VOICE);
    nfifo = 0;
}

void music_init(void) {
    for (int i = 0; i < NBUF; i++) {
        buf[i] = memalign(32, BUFBYTES);
        memset(buf[i], 0, BUFBYTES);
    }
    silence = memalign(32, SILENCE_BYTES);
    memset(silence, 0, SILENCE_BYTES);
    DCFlushRange(silence, SILENCE_BYTES);
    void *stack = memalign(32, 32768);
    LWP_CreateThread(&thread, worker, NULL, stack, 32768, 70);
}

/* ig.music.next() with ig.music.random: a random track, from the start */
void music_next(void) {
    stop_voice();
    track = rand() % TRACKS;
    restart = 1;
    while (restart) usleep(1000);
    cbIdx = 0;
    fading = 0;
    lastVol = -1;
    want = 1;
}

void music_fade_out(float seconds) {
    if (!want) return;
    if (seconds <= 0 || !playing) { stop_voice(); want = 0; return; }
    fading = 1;
    fadeLeft = fadeTotal = seconds;
}

void music_set_volume(float v) { volume = v; }

void music_update(float dt) {
    if (!want) return;
    int v = level(volume);
    if (!playing) {
        /* start once two buffers are decoded */
        if (st[0] == 1 && st[1] == 1) {
            nfifo = 0;
            playing = 1;
            ASND_SetVoice(MUSIC_VOICE, ch == 2 ? VOICE_STEREO_16BIT : VOICE_MONO_16BIT, rate, 0,
                          buf[0], blen[0], v, v, voice_cb);
            st[0] = 2; fifo[nfifo++] = 0;
            ASND_AddVoice(MUSIC_VOICE, buf[1], blen[1]);
            st[1] = 2; fifo[nfifo++] = 1;
            cbIdx = 2;
            lastVol = v;
        }
        return;
    }
    if (fading) {
        fadeLeft -= dt;
        if (fadeLeft <= 0) { stop_voice(); want = 0; fading = 0; return; }
        v = (int)(v * (fadeLeft / fadeTotal));
    }
    if (v != lastVol) {
        ASND_ChangeVolumeVoice(MUSIC_VOICE, v, v);
        lastVol = v;
    }
}
