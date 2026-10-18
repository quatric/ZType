#ifndef ZT_H
#define ZT_H

/*
 * Shared pieces of the ZType port: impact.js style timers, the per-frame input state and the
 * helpers that the game, the menus and the SD card storage all use.
 */
#include <gccore.h>
#include <math.h>
#include <stdlib.h>
#include "gx_render.h"
#include "sound.h"

#define ZT_PI 3.14159265358979f

/* ig.Timer: delta() starts at -seconds and reaches 0 when the time is up */
typedef struct { double base; float target; } Timer;

extern double zt_time;   /* ig.Timer.time, advanced once per frame */
extern float zt_tick;    /* ig.system.tick, seconds of the last frame (capped at 50ms) */

static inline void timer_init(Timer *t, float seconds) { t->base = zt_time; t->target = seconds; }
static inline void timer_set(Timer *t, float seconds) { t->base = zt_time; t->target = seconds; }
static inline void timer_reset(Timer *t) { t->base = zt_time; }
static inline float timer_delta(const Timer *t) { return (float)(zt_time - t->base - t->target); }

/* Math.random() and the few Number helpers of impact.js */
static inline float frand(void) { return (float)(rand() & 0xFFFFFF) / 16777216.0f; }
static inline float fmap(float v, float is, float ie, float os, float oe) { return (v - is) / (ie - is) * (oe - os) + os; }
static inline float flimit(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float fround(float v) { return floorf(v + 0.5f); }

/* game.ease */
static inline float ease_in_out_quad(float t, float start, float c, float duration) {
    t /= duration / 2;
    if (t < 1) return c / 2 * t * t + start;
    --t;
    return -c / 2 * (t * (t - 2) - 1) + start;
}

/* ig.input: one-frame "pressed" flags plus the pointer */
typedef struct {
    int ok;         /* Enter, Space, Wii A */
    int enter;      /* Enter or Wii B: fires the EMP in a game */
    int menu;       /* Escape, Wii + */
    int up, down, left, right;
    int back;       /* Backspace, Wii -: cancels the current target */
    int info;       /* I, Wii 1: credits */
    int click;      /* pointer click */
    int mouse_x, mouse_y;
} Input;
extern Input zt_in;

#endif
