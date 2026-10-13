/* Ogg Vorbis music streamer: a worker thread decodes into a ring of buffers that the
 * ASND voice callback feeds to the audio hardware. */
#include <gccore.h>
#include <asndlib.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>
#include <tremor/ivorbisfile.h>
#include "music.h"

#define MUSIC_VOICE 0
#define FRAMES 4096
#define NBUF 4
#define BUFBYTES (FRAMES * 2 * 2)

static const char *TRACKS[2] = {"sd:/apps/typeshot/music/endure.ogg", "sd:/apps/typeshot/music/orientation.ogg"};

static s16 *buf[NBUF], *sil;
static volatile int st[NBUF];            /* 0 empty, 1 decoded, 2 queued on the voice */
static int blen[NBUF];
static int fillIdx, cbIdx, fifo[3], nfifo;
static volatile int want, playing;
static int curTrack, vfOpen, rate, ch;
static OggVorbis_File vf;
static lwp_t thread;
static int inited;
static float fadeLeft, fadeTotal;
static int fading, lastVol = -1;

int music_available(void) {
	for (int i = 0; i < 2; i++) { FILE *f = fopen(TRACKS[i], "rb"); if (f) { fclose(f); return 1; } }
	return 0;
}
int music_active(void) { return want; }

static int openTrack(void) {
	for (int k = 0; k < 2; k++) {
		FILE *f = fopen(TRACKS[curTrack], "rb");
		if (f) {
			if (ov_open(f, &vf, NULL, 0) >= 0) {
				vorbis_info *vi = ov_info(&vf, -1);
				if (vi && vi->channels >= 1 && vi->channels <= 2) { rate = (int)vi->rate; ch = vi->channels; vfOpen = 1; return 1; }
				ov_clear(&vf);
			} else fclose(f);
		}
		curTrack ^= 1;
	}
	return 0;
}

static void *worker(void *arg) {
	(void)arg;
	for (;;) {
		if (!want) {
			if (vfOpen) { ov_clear(&vf); vfOpen = 0; }
			for (int i = 0; i < NBUF; i++) st[i] = 0;
			fillIdx = 0;
			usleep(30000);
			continue;
		}
		if (!vfOpen && !openTrack()) { usleep(500000); continue; }
		int s = fillIdx;
		if (st[s]) { usleep(4000); continue; }
		int bytes = FRAMES * ch * 2, got = 0;
		while (got < bytes && want) {
			int sec;
			long r = ov_read(&vf, (char *)buf[s] + got, bytes - got, &sec);
			if (r <= 0) { ov_clear(&vf); vfOpen = 0; curTrack ^= 1; if (!openTrack()) break; }
			else got += r;
		}
		if (!want) continue;
		if (got < bytes) memset((char *)buf[s] + got, 0, bytes - got);
		blen[s] = bytes;
		DCFlushRange(buf[s], bytes);
		st[s] = 1;
		fillIdx = (s + 1) % NBUF;
	}
	return NULL;
}

static void voiceCb(s32 voice) {
	if (!playing) return;
	if (nfifo > 0) {
		int done = fifo[0];
		fifo[0] = fifo[1]; nfifo--;
		if (done >= 0) st[done] = 0;
	}
	int n = cbIdx;
	if (st[n] == 1) {
		ASND_AddVoice(voice, buf[n], blen[n]);
		st[n] = 2; fifo[nfifo++] = n; cbIdx = (n + 1) % NBUF;
	} else {
		ASND_AddVoice(voice, sil, 4096);
		fifo[nfifo++] = -1;
	}
}

void music_init(void) {
	if (inited) return;
	inited = 1;
	for (int i = 0; i < NBUF; i++) { buf[i] = memalign(32, BUFBYTES); memset(buf[i], 0, BUFBYTES); }
	sil = memalign(32, 4096); memset(sil, 0, 4096); DCFlushRange(sil, 4096);
	void *stack = memalign(32, 32768);
	LWP_CreateThread(&thread, worker, NULL, stack, 32768, 70);
}

void music_start(void) {
	if (!inited) music_init();
	if (playing) { ASND_StopVoice(MUSIC_VOICE); playing = 0; }
	curTrack = (int)(gettime() & 1);
	nfifo = 0; cbIdx = 0; fading = 0; lastVol = -1;
	want = 1;
}

void music_stop(float fade) {
	if (!want) return;
	if (fade <= 0.f || !playing) { playing = 0; ASND_StopVoice(MUSIC_VOICE); want = 0; return; }
	fading = 1; fadeLeft = fadeTotal = fade;
}

void music_update(float dt, int vol) {
	if (!want) return;
	if (!playing) {
		/* start once two buffers are decoded */
		if (st[0] == 1 && st[1] == 1 && ch) {
			nfifo = 0; cbIdx = 0;
			playing = 1;
			ASND_SetVoice(MUSIC_VOICE, ch == 2 ? VOICE_STEREO_16BIT : VOICE_MONO_16BIT, rate, 0, buf[0], blen[0], vol, vol, voiceCb);
			st[0] = 2; fifo[nfifo++] = 0;
			ASND_AddVoice(MUSIC_VOICE, buf[1], blen[1]);
			st[1] = 2; fifo[nfifo++] = 1; cbIdx = 2;
			lastVol = vol;
		}
		return;
	}
	int v = vol;
	if (fading) {
		fadeLeft -= dt;
		if (fadeLeft <= 0.f) { playing = 0; ASND_StopVoice(MUSIC_VOICE); want = 0; fading = 0; return; }
		v = (int)(vol * (fadeLeft / fadeTotal));
	}
	if (v != lastVol) { ASND_ChangeVolumeVoice(MUSIC_VOICE, v, v); lastVol = v; }
}
