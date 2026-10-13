/* Procedurally synthesized sound effects and looping music via ASND. */
#include <gccore.h>
#include <asndlib.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "sound.h"

#define RATE 32000
#define MUSIC_VOICE 0
#define MUSIC_VOL 70
#define SFX_VOL 230

static float sfxGain = .5f, musGain = .5f;
static int vol(int base, float g) { int v = (int)(base * g * 2.f); return v > 255 ? 255 : v; }

static s16 *sfx[SFX_COUNT];
static int sfxLen[SFX_COUNT];
static s16 *music;
static int musicLen;
static int muted, nextVoice = 1;
static unsigned nz = 1;

static float noise(void) { nz = nz * 1664525u + 1013904223u; return ((nz >> 16) & 0x7fff) / 16384.f - 1.f; }
static float sq(float ph) { return (ph - floorf(ph)) < 0.5f ? 1.f : -1.f; }
static float saw(float ph) { return 2.f * (ph - floorf(ph)) - 1.f; }
static float tri(float ph) { float p = ph - floorf(ph); return 4.f * fabsf(p - 0.5f) - 1.f; }

static s16 *alloc(int n, int *outLen) {
	int bytes = (n * 2 + 31) & ~31;
	s16 *b = memalign(32, bytes);
	memset(b, 0, bytes);
	*outLen = bytes;
	return b;
}
static void finish(s16 *b, int bytes) { DCFlushRange(b, bytes); }

static float midi(int n) { return 440.f * powf(2.f, (n - 69) / 12.f); }

static void mkSfx(int id, float secs, float (*gen)(float t, float dur)) {
	int n = (int)(secs * RATE);
	s16 *b = alloc(n, &sfxLen[id]);
	for (int i = 0; i < n; i++) {
		float v = gen(i / (float)RATE, secs);
		if (v > 1.f) v = 1.f;
		if (v < -1.f) v = -1.f;
		b[i] = (s16)(v * 20000);
	}
	finish(b, sfxLen[id]);
	sfx[id] = b;
}

static float gShoot(float t, float d) {
	float f = 1500.f - 900.f * (t / d), env = 1.f - t / d;
	static float ph; ph += f / RATE;
	return sq(ph) * env * 0.5f;
}
static float gMiss(float t, float d) {
	static float ph; ph += 95.f / RATE;
	return (saw(ph) * 0.6f + noise() * 0.2f) * (1.f - t / d);
}
static float gExplode(float t, float d) {
	static float lp; float env = powf(1.f - t / d, 2.f);
	lp += (noise() - lp) * (0.25f - 0.2f * t / d);
	return lp * 2.2f * env;
}
static float gBreach(float t, float d) {
	static float ph; float f = 90.f - 55.f * (t / d); ph += f / RATE;
	return (sinf(ph * 6.2832f) * 0.9f + noise() * 0.25f) * (1.f - t / d);
}
static float gLevel(float t, float d) {
	static const int notes[4] = {72, 76, 79, 84};
	int k = (int)(t / d * 4); if (k > 3) k = 3;
	static float ph; ph += midi(notes[k]) / RATE;
	float local = (t / d * 4) - k;
	return tri(ph) * (1.f - local * 0.5f) * 0.7f;
}
static float gOver(float t, float d) {
	static const int notes[4] = {64, 62, 60, 55};
	int k = (int)(t / d * 4); if (k > 3) k = 3;
	static float ph; ph += midi(notes[k]) / RATE;
	return saw(ph) * (1.f - t / d) * 0.5f;
}


static float gExplodeL(float t, float d) {
	static float lp; float env = powf(1.f - t / d, 1.6f);
	lp += (noise() - lp) * (0.12f - 0.1f * t / d);
	return lp * 3.2f * env;
}
static float gTarget(float t, float d) {
	static float ph; ph += (900.f + 700.f * (t / d)) / RATE;
	return sinf(ph * 6.2832f) * (1.f - t / d) * 0.6f;
}
static float gHit(float t, float d) {
	static float ph; ph += 320.f / RATE;
	return (sinf(ph * 6.2832f) * 0.5f + noise() * 0.4f) * powf(1.f - t / d, 2.f);
}
static float gEmp(float t, float d) {
	static float ph, lp; ph += (70.f - 40.f * (t / d)) / RATE;
	lp += (noise() - lp) * 0.08f;
	return (sinf(ph * 6.2832f) * 0.8f + lp * 2.f) * (1.f - t / d);
}
static float gClick(float t, float d) {
	static float ph; ph += 1300.f / RATE;
	return sq(ph) * powf(1.f - t / d, 2.f) * 0.4f;
}
static float gCancel(float t, float d) {
	static float ph; ph += (720.f - 420.f * (t / d)) / RATE;
	return tri(ph) * (1.f - t / d) * 0.6f;
}
static float gSpawn(float t, float d) {
	static float ph; ph += (180.f + 900.f * (t / d) * (t / d)) / RATE;
	return sinf(ph * 6.2832f) * sinf(3.1416f * t / d) * 0.6f;
}
static float gMulti(float t, float d) {
	static float ph; int k = t / d > 0.5f; ph += midi(k ? 84 : 79) / RATE;
	return tri(ph) * (1.f - (t / d - k * 0.5f) * 1.2f) * 0.6f;
}

static void mkMusic(void) {
	/* 8 bars at 120 bpm (2s per bar) = 16s loop: Am F C G Am F G E */
	static const int root[8] = {45, 41, 48, 43, 45, 41, 43, 40};
	static const int chord[8][3] = {{0,3,7},{0,4,7},{0,4,7},{0,4,7},{0,3,7},{0,4,7},{0,4,7},{0,4,7}};
	static const int minor[8] = {1,0,0,0,1,0,0,0};
	(void)minor;
	float beat = 0.25f;                     /* 8th notes of 0.25s */
	int n = (int)(16.f * RATE);
	music = alloc(n, &musicLen);
	float pb = 0, pa = 0;
	for (int i = 0; i < n; i++) {
		float t = i / (float)RATE;
		int bar = (int)(t / 2.f) % 8;
		float inBar = t - bar * 2.f;
		int step = (int)(inBar / beat);       /* 0..7 */
		float inStep = (inBar - step * beat) / beat;
		/* bass: pulse on each 8th */
		pb += midi(root[bar]) / RATE;
		float bass = tri(pb) * (1.f - inStep * 0.6f) * 0.55f;
		/* arpeggio */
		int note = root[bar] + 24 + chord[bar][(step * 2 + (step / 4)) % 3];
		pa += midi(note) / RATE;
		float arp = sq(pa) * (1.f - inStep) * 0.18f;
		/* hat on off-beats */
		float hat = (step & 1) ? noise() * (1.f - inStep) * (1.f - inStep) * 0.12f : 0.f;
		/* kick on quarter notes */
		float kick = 0;
		if (!(step & 1) && inStep < 0.5f) kick = sinf(2 * 3.14159f * (60.f + 90.f * (0.5f - inStep)) * (inStep * beat)) * (1.f - inStep * 2.f) * 0.5f;
		float v = bass + arp + hat + kick;
		/* fade seam for a click-free loop */
		if (v > 1.f) v = 1.f;
		if (v < -1.f) v = -1.f;
		music[i] = (s16)(v * 14000);
	}
	music[n - 1] = 0;
	finish(music, musicLen);
}

static void musicLoop(s32 voice) {
	ASND_AddVoice(voice, music, musicLen);
}

static void musicStart(void) {
	ASND_SetVoice(MUSIC_VOICE, VOICE_MONO_16BIT, RATE, 0, music, musicLen,
		muted ? 0 : vol(MUSIC_VOL, musGain), muted ? 0 : vol(MUSIC_VOL, musGain), musicLoop);
	ASND_AddVoice(MUSIC_VOICE, music, musicLen);
}

void sound_init(void) {
	AUDIO_Init(NULL);
	ASND_Init();
	ASND_Pause(0);
	mkSfx(SFX_SHOOT, 0.07f, gShoot);
	mkSfx(SFX_MISS, 0.18f, gMiss);
	mkSfx(SFX_EXPLODE, 0.45f, gExplode);
	mkSfx(SFX_EXPLODE_L, 0.8f, gExplodeL);
	mkSfx(SFX_TARGET, 0.06f, gTarget);
	mkSfx(SFX_HIT, 0.08f, gHit);
	mkSfx(SFX_EMP, 0.9f, gEmp);
	mkSfx(SFX_CLICK, 0.03f, gClick);
	mkSfx(SFX_CANCEL, 0.1f, gCancel);
	mkSfx(SFX_SPAWN, 0.5f, gSpawn);
	mkSfx(SFX_MULTI, 0.3f, gMulti);
	mkSfx(SFX_BREACH, 0.5f, gBreach);
	mkSfx(SFX_LEVEL, 0.5f, gLevel);
	mkSfx(SFX_OVER, 1.0f, gOver);
	mkMusic();
	musicStart();
}

void sound_play(int id) {
	if (muted || id < 0 || id >= SFX_COUNT || !sfx[id]) return;
	int v = nextVoice;
	nextVoice = nextVoice >= 9 ? 1 : nextVoice + 1;
	ASND_SetVoice(v, VOICE_MONO_16BIT, RATE, 0, sfx[id], sfxLen[id], vol(SFX_VOL, sfxGain), vol(SFX_VOL, sfxGain), NULL);
}

void sound_toggle_mute(void) {
	muted = !muted;
	ASND_ChangeVolumeVoice(MUSIC_VOICE, muted ? 0 : vol(MUSIC_VOL, musGain), muted ? 0 : vol(MUSIC_VOL, musGain));
}
int sound_muted(void) { return muted; }

void sound_set_sfx_volume(float v) { sfxGain = v; }
void sound_set_music_volume(float v) {
	musGain = v;
	ASND_ChangeVolumeVoice(MUSIC_VOICE, muted ? 0 : vol(MUSIC_VOL, musGain), muted ? 0 : vol(MUSIC_VOL, musGain));
}
