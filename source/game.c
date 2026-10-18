/*
 * ZType, ported from zty.pe's game code (impact.js entities, waves, targeting and drawing).
 * Numbers and update order follow the original so it looks and plays the same.
 */
#include "game.h"
#include "menus.h"
#include "stats.h"
#include "music.h"
#include "words.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

ZGame zg;

/* ---------------------------------------------------------------- constants of the original */

#define SYS_W CANVAS_W
#define SYS_H CANVAS_H
#define KB_DRAW_SCALE 0.75f            /* ig.Keyboard.drawScale: 480 / 640 */
#define KB_Y 896.0f                    /* 720 / 0.75 - 64 */
#define PLAYER_SY (SYS_H - 64 * KB_DRAW_SCALE - 30)

enum { TYPE_NONE = 0, TYPE_A = 1, TYPE_B = 2 };

typedef enum {
    ENT_PLAYER, ENT_PLASMA, ENT_MINE, ENT_MISSLE, ENT_DESTROYER, ENT_OPPRESSOR, ENT_BULLET,
    ENT_PARTICLE, ENT_SPARK, ENT_HUGE, ENT_HUGE_GLITCH, ENT_EMP
} EntType;

/* ---------------------------------------------------------------- ig.Animation */

typedef struct {
    const int *seq;
    int len;
    float frameTime;
    int stop;
    Timer timer;
    int loopCount, frame, tile;
    float angle, alpha;
    int flipx, flipy;
} Anim;

static void anim_init(Anim *a, const int *seq, int len, float frameTime, int stop) {
    memset(a, 0, sizeof(*a));
    a->seq = seq;
    a->len = len;
    a->frameTime = frameTime;
    a->stop = stop;
    timer_init(&a->timer, 0);
    a->tile = seq[0];
    a->alpha = 1.0f;
}

static void anim_update(Anim *a) {
    int frameTotal = (int)floorf(timer_delta(&a->timer) / a->frameTime);
    a->loopCount = (int)floorf((float)frameTotal / a->len);
    if (a->stop && a->loopCount > 0) a->frame = a->len - 1;
    else {
        a->frame = frameTotal % a->len;
        if (a->frame < 0) a->frame += a->len;
    }
    a->tile = a->seq[a->frame];
}

static void anim_rewind(Anim *a) {
    timer_set(&a->timer, 0);
    a->loopCount = 0;
    a->frame = 0;
    a->tile = a->seq[0];
}

static void anim_goto_frame(Anim *a, int f) {
    timer_set(&a->timer, a->frameTime * -f - 0.0001f);
    anim_update(a);
}

/* ---------------------------------------------------------------- entities */

struct Entity {
    int type;                 /* EntType */
    int id, used, killed;
    int ety, checkAgainst;       /* ig.Entity.type / checkAgainst flags */
    float x, y, vx, vy;
    float sx, sy, ox, oy;     /* size, offset */
    float fx, fy, maxvx, maxvy;
    float health;

    const Spr *sheet;
    int tw, th;
    Anim anims[3];
    int cur;

    /* enemies */
    char word[16], rem[16];
    int targeted, dead;
    float angle, speed;
    Timer hitTimer, reticleTimer, shootTimer, targetTimer;
    int hasReticle;
    int wmin, wmax, nkill, nhit;
    int sfx;

    /* plasma */
    Entity *target;
    int target_id;
    float distance;

    /* particles, explosions */
    Timer idleTimer;
    float lifetime, fadetime, alpha;

    /* player */
    float targetAngle;
};

#define MAX_ENTS 512
static Entity pool[MAX_ENTS];
static Entity *ents[MAX_ENTS];
static int nents;
static Entity *deferred[MAX_ENTS];
static int ndeferred;
static int next_id, next_slot;

static const int SEQ_IDLE[] = {0};
static const int SEQ_SHOOT[] = {3, 2, 1, 0};
static const int SEQ_MISS[] = {4, 5, 6};
static const int SEQ_EXPL[] = {0, 1, 2};

static Entity *ent_alloc(void) {
    for (int n = 0; n < MAX_ENTS; n++) {
        int i = (next_slot + n) % MAX_ENTS;
        if (!pool[i].used) {
            next_slot = i + 1;
            Entity *e = &pool[i];
            memset(e, 0, sizeof(*e));
            e->used = 1;
            e->id = ++next_id;
            e->sx = e->sy = 16;
            e->maxvx = e->maxvy = 100;
            e->sfx = -1;
            return e;
        }
    }
    return NULL;
}

static void ents_push(Entity *e) { if (nents < MAX_ENTS) ents[nents++] = e; }

static void ents_unshift(Entity *e) {
    if (nents >= MAX_ENTS) return;
    memmove(&ents[1], &ents[0], nents * sizeof(ents[0]));
    ents[0] = e;
    nents++;
}

static void ents_erase(Entity *e) {
    for (int i = 0; i < nents; i++) {
        if (ents[i] == e) {
            memmove(&ents[i], &ents[i + 1], (nents - i - 1) * sizeof(ents[0]));
            nents--;
            return;
        }
    }
}

static void ent_remove(Entity *e) {            /* ig.game.removeEntity */
    e->killed = 1;
    e->ety = TYPE_NONE;
    e->checkAgainst = TYPE_NONE;
    if (ndeferred < MAX_ENTS) deferred[ndeferred++] = e;
}

static float ent_cx(const Entity *e) { return e->x + e->sx / 2; }
static float ent_cy(const Entity *e) { return e->y + e->sy / 2; }

static float distance_to(const Entity *a, const Entity *b) {
    float xd = ent_cx(a) - ent_cx(b), yd = ent_cy(a) - ent_cy(b);
    return sqrtf(xd * xd + yd * yd);
}

static float angle_to(const Entity *a, const Entity *b) {
    return atan2f(ent_cy(b) - ent_cy(a), ent_cx(b) - ent_cx(a));
}

static int ent_touches(const Entity *a, const Entity *b) {
    return !(a->x >= b->x + b->sx || a->x + a->sx <= b->x || a->y >= b->y + b->sy || a->y + a->sy <= b->y);
}

/* ig.Entity.update */
static float new_velocity(float vel, float friction, float max) {
    if (friction) {
        float delta = friction * zt_tick;
        if (vel - delta > 0) return vel - delta;
        if (vel + delta < 0) return vel + delta;
        return 0;
    }
    return flimit(vel, -max, max);
}

static void ent_update_base(Entity *e) {
    e->vx = new_velocity(e->vx, e->fx, e->maxvx);
    e->vy = new_velocity(e->vy, e->fy, e->maxvy);
    e->x += e->vx * zt_tick;
    e->y += e->vy * zt_tick;
    anim_update(&e->anims[e->cur]);
}

/* ig.Animation.draw */
static void ent_draw_anim(const Entity *e, float tx, float ty) {
    const Anim *a = &e->anims[e->cur];
    float bb = e->tw > e->th ? e->tw : e->th;
    if (tx > SYS_W || ty > SYS_H || tx + bb < 0 || ty + bb < 0) return;
    if (a->alpha != 1) gx_alpha(a->alpha);
    if (a->angle == 0) {
        gx_spr_tile(e->sheet, a->tile, e->tw, e->th, tx, ty, a->flipx, a->flipy);
    } else {
        float px = e->tw / 2.0f, py = e->th / 2.0f;
        gx_save();
        gx_translate(tx + px, ty + py);
        gx_rotate(a->angle);
        gx_spr_tile(e->sheet, a->tile, e->tw, e->th, -px, -py, a->flipx, a->flipy);
        gx_restore();
    }
    if (a->alpha != 1) gx_alpha(1);
}

static void ent_draw_base(const Entity *e) {
    ent_draw_anim(e, e->x - e->ox - zg.rscreen_x, e->y - e->oy - zg.rscreen_y);
}

/* ---------------------------------------------------------------- game state */

typedef struct { int kind; const char *word; float wait; } SpawnItem;
typedef struct { int kind, count, incEvery; } WaveType;

static struct {
    int wave;
    float spawnWait, currentSpawnWait, speedIncrease;
    WaveType types[3];
    SpawnItem spawn[160];
    int nspawn;
} W;

static float speedFactor;
static Timer lastKillTimer, spawnTimer, idleTimer, waveEndTimer;
static int hasWaveEnd;
static float screenShakeAmount;
static Entity *currentTarget;
static char buffered[64];
static int nbuffered;

/* keyboard HUD */
static Timer multiplierTimer;
static int hasMultiplierTimer, multiplierIndex;

/* custom text ("load your own text") */
static char *wl_text;
static const char *wl[2048];
static int nwl;
static const char *wl_queue[2048];
static int nwl_queue;

static void screen_shake(float strength) {
    if (strength > screenShakeAmount) screenShakeAmount = strength;
}

/* ---------------------------------------------------------------- entity factories */

static Entity *spawn_particle(int kind, float x, float y, float vel) {
    Entity *e = ent_alloc();
    if (!e) return NULL;
    e->type = kind;
    e->x = x;
    e->y = y;
    e->sx = e->sy = 4;
    e->ox = e->oy = 0;
    e->lifetime = 0.5f;
    e->fadetime = 0.5f;
    e->fx = 20;
    e->fy = 0;
    e->maxvx = e->maxvy = 1000;
    if (kind == ENT_PARTICLE) {
        e->sheet = &SPR_EXPLOSION;
        e->tw = e->th = 32;
        anim_init(&e->anims[0], SEQ_EXPL, 3, 5, 0);
    } else {
        e->sheet = &SPR_SPARK;
        e->tw = e->th = 48;
        anim_init(&e->anims[0], SEQ_IDLE, 1, 1, 0);
    }
    e->vx = (frand() * 2 - 1) * vel;
    e->vy = (frand() * 2 - 1) * vel;
    Anim *a = &e->anims[0];
    a->flipx = frand() > 0.5f;
    a->flipy = frand() > 0.5f;
    anim_goto_frame(a, (int)floorf(frand() * a->len));
    timer_init(&e->idleTimer, 0);
    ents_push(e);
    return e;
}

static void spawn_huge(int kind, float x, float y) {
    Entity *e = ent_alloc();
    if (!e) return;
    e->type = kind;
    e->x = x;
    e->y = y;
    e->lifetime = 1;
    e->fadetime = 1;
    timer_init(&e->idleTimer, 0);
    ents_push(e);
}

static void spawn_emp(float x, float y) {
    Entity *e = ent_alloc();
    if (!e) return;
    e->type = ENT_EMP;
    e->x = x;
    e->y = y;
    e->lifetime = 1;
    e->fadetime = 1;
    timer_init(&e->idleTimer, 0);
    sound_play(SFX_EMP);
    screen_shake(30);
    ents_push(e);
}

static const char *random_word(int l) {
    if (l >= 2 && l <= 12) return WORDS_BY_LEN[l][(int)(frand() * WORDS_COUNT_BY_LEN[l])];
    static char one[2];
    one[0] = (char)('a' + (int)(frand() * 26));
    one[1] = 0;
    return one;
}

static int is_registered_letter(char c) {
    for (int i = 0; i < nents; i++) {
        Entity *e = ents[i];
        if (e->type >= ENT_MINE && e->type <= ENT_BULLET && !e->killed && !e->targeted && e->rem[0] == c) return 1;
    }
    return 0;
}

static void enemy_set_word(Entity *e, const char *forced) {
    char w[16];
    if (forced) {
        snprintf(w, sizeof(w), "%s", forced);
    } else {
        int length = (int)fround(fmap(frand(), 0, 1, e->wmin, e->wmax));
        strcpy(w, "wtf");
        for (int i = 0; i < 20; i++) {
            snprintf(w, sizeof(w), "%s", random_word(length));
            if (!is_registered_letter((char)tolower((unsigned char)w[0]))) break;
        }
    }
    snprintf(e->word, sizeof(e->word), "%s", w);
    snprintf(e->rem, sizeof(e->rem), "%s", w);
    e->health = (float)strlen(w);
}

static Entity *spawn_enemy(int type, float x, float y, const char *word, float angle_setting, int at_beginning) {
    Entity *e = ent_alloc();
    if (!e) return NULL;
    e->type = type;
    e->ety = TYPE_B;
    e->x = x;
    e->y = y;
    e->checkAgainst = TYPE_A;
    e->fx = e->fy = 100;
    e->wmin = e->wmax = 8;
    e->nkill = 20;
    e->nhit = 5;
    e->speed = 10;
    e->health = 8;
    timer_init(&e->hitTimer, 0);

    switch (type) {
    case ENT_MINE:
        e->sx = e->sy = 12; e->ox = e->oy = 10;
        e->sheet = &SPR_MINE; e->tw = e->th = 32;
        e->speed = 30; e->wmin = 3; e->wmax = 6; e->sfx = SFX_EXPLODE_M;
        break;
    case ENT_MISSLE:
        e->sx = 8; e->sy = 15; e->ox = 6; e->oy = 7;
        e->sheet = &SPR_MISSLE; e->tw = 20; e->th = 26;
        e->speed = 35; e->wmin = 2; e->wmax = 5; e->nkill = 10; e->nhit = 2; e->sfx = SFX_EXPLODE_S;
        break;
    case ENT_DESTROYER:
        e->sx = 24; e->sy = 34; e->ox = 10; e->oy = 8;
        e->sheet = &SPR_DESTROYER; e->tw = 43; e->th = 58;
        e->speed = 20; e->wmin = 7; e->wmax = 10; e->nkill = 40; e->nhit = 10; e->sfx = SFX_EXPLODE_L;
        break;
    case ENT_OPPRESSOR:
        e->sx = 36; e->sy = 58; e->ox = 16; e->oy = 10;
        e->sheet = &SPR_OPPRESSOR; e->tw = 68; e->th = 88;
        e->speed = 15; e->wmin = 9; e->wmax = 12; e->nkill = 40; e->nhit = 10; e->sfx = SFX_EXPLODE_L;
        e->y = y - 18;
        break;
    case ENT_BULLET:
        e->sx = e->sy = 2; e->ox = 8; e->oy = 11;
        e->sheet = &SPR_BULLET; e->tw = 20; e->th = 24;
        e->speed = 50; e->wmin = e->wmax = 1;
        break;
    }
    anim_init(&e->anims[0], SEQ_IDLE, 1, 1, 0);

    enemy_set_word(e, word);
    e->angle = angle_to(e, zg.player);
    e->speed *= speedFactor;

    switch (type) {
    case ENT_MISSLE:
        e->angle = angle_setting;
        e->anims[0].angle = e->angle - ZT_PI / 2;
        timer_init(&e->targetTimer, 1);
        break;
    case ENT_DESTROYER:
        timer_init(&e->shootTimer, 5);
        e->angle = (fmap(frand(), 0, 1, 67, 90) + (e->x > SYS_W / 2 ? 22.5f : 0)) * ZT_PI / 180;
        e->anims[0].angle = e->angle - ZT_PI / 2;
        break;
    case ENT_OPPRESSOR:
        timer_init(&e->shootTimer, 7);
        e->angle = ZT_PI / 2;
        break;
    case ENT_BULLET:
        e->angle = angle_setting;
        e->anims[0].angle = e->angle - ZT_PI / 2;
        break;
    }

    if (at_beginning) ents_unshift(e);
    else ents_push(e);
    return e;
}

/* ---------------------------------------------------------------- enemies */

static void spawn_explosion_particles(Entity *e, int killed) {
    int num = killed ? e->nkill : e->nhit;
    for (int i = 0; i < num; i++) spawn_particle(ENT_PARTICLE, e->x, e->y, 60);
    float sparks = num / 2.0f;
    for (int i = 0; i < sparks; i++) spawn_particle(ENT_SPARK, e->x, e->y, 60);
}

static void enemy_kill(Entity *e, int silent) {
    if (currentTarget == e) currentTarget = NULL;
    ent_remove(e);
    if (e->sfx >= 0 && !silent) sound_play(e->sfx);
    if (!silent) {
        timer_set(&lastKillTimer, 0.3f);
        float px = e->x - e->sx / 2, py = e->y - e->sy / 2;
        for (int i = 0; i < e->wmax; i++) spawn_particle(ENT_SPARK, px, py, 360);
    }
    if (e->type == ENT_MINE) {
        screen_shake(5);
    } else if (e->type == ENT_DESTROYER) {
        if (!silent) { spawn_huge(ENT_HUGE, e->x + 12, e->y + 22); screen_shake(10); }
    } else if (e->type == ENT_OPPRESSOR) {
        if (!silent) { spawn_huge(ENT_HUGE, e->x + 12, e->y + 22); screen_shake(20); }
    }
}

static void enemy_hit(Entity *e) {
    spawn_explosion_particles(e, e->health <= 1);
    e->vx = -cosf(e->angle) * 20 + frand() * 20;
    e->vy = -sinf(e->angle) * 20 + frand() * 20;
    timer_set(&e->hitTimer, 0.3f);
    timer_set(&lastKillTimer, 0.2f);
    e->health -= 1;
    if (e->health <= 0) enemy_kill(e, 0);
    sound_play(SFX_HIT);
}

static void enemy_update(Entity *e) {
    if (timer_delta(&e->hitTimer) > 0) {
        e->vx = cosf(e->angle) * e->speed;
        e->vy = sinf(e->angle) * e->speed;
    }
    ent_update_base(e);
    if (e->x < -e->tw || e->x > SYS_W + 10 || e->y > SYS_H + 10 || e->y < -e->th - 30) enemy_kill(e, 1);
    if (e->hasReticle && timer_delta(&e->reticleTimer) > 0) e->hasReticle = 0;
}

static void update_missle(Entity *e) {
    float d = timer_delta(&e->targetTimer);
    if (d > 0 && d < 0.7f) {
        float ad = e->angle - angle_to(e, zg.player);
        e->angle -= ad * zt_tick * 2;
        e->anims[0].angle = e->angle - ZT_PI / 2;
    }
    enemy_update(e);
}

static void update_mine(Entity *e) {
    e->angle = angle_to(e, zg.player);
    enemy_update(e);
    e->anims[0].angle += 2 * zt_tick;
}

static void update_destroyer(Entity *e) {
    enemy_update(e);
    if (timer_delta(&e->shootTimer) > 0) {
        timer_reset(&e->shootTimer);
        if (distance_to(e, zg.player) > 100) spawn_enemy(ENT_MISSLE, e->x + 12, e->y + 22, NULL, e->angle, 0);
    }
}

static void update_oppressor(Entity *e) {
    enemy_update(e);
    if (timer_delta(&e->shootTimer) > 0) {
        if (distance_to(e, zg.player) > 100) {
            const int bullets = 8;
            float inc = 100.0f / (bullets - 1);
            float a = 40;
            for (int i = 0; i < bullets; i++) {
                float angle = a * ZT_PI / 180;
                float x = e->x + 18 + cosf(angle) * 21;
                float y = e->y + 48 + sinf(angle) * 21;
                spawn_enemy(ENT_BULLET, x, y, NULL, angle, 0);
                a += inc;
            }
        }
        timer_reset(&e->shootTimer);
    }
}

/* ig.game.player.kill() is reached through an enemy's check() */
static void player_kill(Entity *p) {
    game_set_game_over();
    sound_play(SFX_EXPLODE_P);
    spawn_huge(ENT_HUGE_GLITCH, p->x + 12, p->y + 22);
    screen_shake(10);
    p->y = SYS_H + 300;
    ent_remove(p);
}

/* ---------------------------------------------------------------- player, plasma, emp */

static void player_shoot(Entity *p, Entity *target) {
    p->cur = 1;
    anim_rewind(&p->anims[1]);
    Entity *pl = ent_alloc();
    if (pl) {
        pl->type = ENT_PLASMA;
        pl->x = p->x + 6;
        pl->y = p->y + 4;
        pl->sx = pl->sy = 4;
        pl->ox = pl->oy = 46;
        pl->maxvx = pl->maxvy = 10000;
        pl->speed = 800;
        pl->sheet = &SPR_PLASMA;
        pl->tw = pl->th = 96;
        anim_init(&pl->anims[0], SEQ_IDLE, 1, 1, 0);
        pl->distance = 100000;
        pl->target = target;
        pl->target_id = target->id;
        ents_push(pl);
    }
    p->targetAngle = angle_to(p, target) + ZT_PI / 2;
    sound_play(SFX_PLASMA);
}

static void player_miss(Entity *p) {
    p->cur = 2;
    anim_rewind(&p->anims[2]);
    sound_play(SFX_CLICK);
}

static void update_player(Entity *p) {
    if (p->anims[p->cur].loopCount > 0) p->cur = 0;
    float ad = p->angle - p->targetAngle;
    if (fabsf(ad) < 0.02f) p->angle = p->targetAngle;
    else p->angle -= ad * zt_tick * 10;
    p->anims[p->cur].angle = p->angle;
    ent_update_base(p);
}

static void update_plasma(Entity *e) {
    Entity *t = e->target;
    if (t && t->id == e->target_id) {
        float cd = distance_to(e, t);
        if (cd > e->distance || cd < t->sy) {
            enemy_hit(t);
            ent_remove(e);
            return;
        }
        float angle = angle_to(e, t);
        e->anims[0].angle = angle + ZT_PI / 2;
        e->vx = cosf(angle) * e->speed;
        e->vy = sinf(angle) * e->speed;
        e->distance = cd;
        ent_update_base(e);
    } else {
        ent_remove(e);
    }
}

static void update_emp(Entity *e) {
    float delta = timer_delta(&e->idleTimer);
    e->alpha = fmap(delta, e->lifetime - e->fadetime, e->lifetime, 1, 0);
    float scale = fmap(e->alpha, 0, 1, 1.7f, 0);
    scale = sqrtf(scale < 0 ? 0 : scale) * 200;
    for (int i = 0; i < nents; i++) {
        Entity *t = ents[i];
        if (t->type >= ENT_MINE && t->type <= ENT_BULLET && distance_to(e, t) < scale) {
            spawn_explosion_particles(t, 1);
            enemy_kill(t, 0);
        }
    }
    if (delta > e->lifetime) ent_remove(e);
}

static void update_particle(Entity *e) {
    Anim *a = &e->anims[0];
    a->angle += 0.1f * zt_tick;
    float delta = timer_delta(&e->idleTimer);
    if (delta > e->lifetime) { ent_remove(e); return; }
    a->alpha = fmap(delta, e->lifetime - e->fadetime, e->lifetime, 1, 0);
    ent_update_base(e);
}

static void update_huge(Entity *e) {
    float delta = timer_delta(&e->idleTimer);
    if (delta > e->lifetime) {
        ent_remove(e);
        if (e->type == ENT_HUGE_GLITCH) game_show_game_over();
        return;
    }
    e->alpha = fmap(delta, e->lifetime - e->fadetime, e->lifetime, 1, 0);
}

static void ent_update(Entity *e) {
    switch (e->type) {
    case ENT_PLAYER:     update_player(e); break;
    case ENT_PLASMA:     update_plasma(e); break;
    case ENT_MINE:       update_mine(e); break;
    case ENT_MISSLE:     update_missle(e); break;
    case ENT_DESTROYER:  update_destroyer(e); break;
    case ENT_OPPRESSOR:  update_oppressor(e); break;
    case ENT_BULLET:     enemy_update(e); break;
    case ENT_PARTICLE:
    case ENT_SPARK:      update_particle(e); break;
    case ENT_HUGE:
    case ENT_HUGE_GLITCH: update_huge(e); break;
    case ENT_EMP:        update_emp(e); break;
    }
}

/* ---------------------------------------------------------------- entity drawing */

static void draw_reticle(const Entity *e) {
    float d = fmap(timer_delta(&e->reticleTimer), -0.5f, 0, 0, 1);
    float scale = (1 - d) * (1 - d) * 4;
    gx_save();
    gx_translate(e->x + e->sx / 2 - zg.rscreen_x, e->y + e->sy / 2 - zg.rscreen_y);
    gx_scale(scale, scale);
    gx_alpha(sqrtf(d < 0 ? 0 : d));
    gx_rotate(d * ZT_PI);
    gx_spr(&SPR_RETICLE, -128, -128);
    gx_restore();
    gx_alpha(1);
}

static void draw_huge(const Entity *e) {
    float scale = fmap(e->alpha, 0, 1, 10, 0);
    int tex = e->type == ENT_HUGE_GLITCH ? TEX_HUGE_GLITCH : TEX_HUGE;
    gx_save();
    gx_translate(e->x - zg.rscreen_x, e->y - zg.rscreen_y);
    gx_scale(scale, scale);
    gx_alpha(e->alpha);
    gx_image(tex, -256, -256);
    gx_restore();
    gx_alpha(1);
    if (e->type == ENT_HUGE_GLITCH) {
        float d = timer_delta(&e->idleTimer);
        if (d < 0.1f || (d > 0.2f && d < 0.3f))
            gx_image(TEX_GL_JPEG, e->x - 240 + (200 - frand() * 400), frand() * SYS_H);
        if (d < 0.15f || (d > 0.25f && d < 0.35f))
            gx_image(TEX_GL_STRIPE, 0, frand() * SYS_H);
    }
}

static void draw_emp(const Entity *e) {
    float s = fmap(e->alpha, 0, 1, 1.7f, 0);
    float scale = sqrtf(s < 0 ? 0 : s);
    gx_save();
    gx_translate(e->x - zg.rscreen_x, e->y - zg.rscreen_y);
    gx_scale(scale * 2, scale * 2);
    gx_rotate(sqrtf(scale) * ZT_PI * 2);
    gx_alpha(e->alpha);
    gx_spr(&SPR_EMP, -128, -128);
    gx_restore();
}

static void ent_draw(const Entity *e) {
    switch (e->type) {
    case ENT_HUGE:
    case ENT_HUGE_GLITCH: draw_huge(e); break;
    case ENT_EMP: draw_emp(e); break;
    default:
        ent_draw_base(e);
        if (e->type >= ENT_MINE && e->type <= ENT_BULLET && e->hasReticle) draw_reticle(e);
        break;
    }
}

static void enemy_draw_label(const Entity *e) {
    if (!e->rem[0]) return;
    float w = enemy_text_width(e->word);
    float x = flimit(e->x - 6, w + 2, SYS_W - 1) + zg.rscreen_x;
    float y = flimit(e->y + e->sy - 10, 2, SYS_H - 19) + zg.rscreen_y;
    gx_alpha(0.75f);
    gx_fill(x - w - 2, y - 1 + 5, w + 8, 24, 0x000000);
    gx_alpha(1);
    enemy_text(e->rem, x + 2, y + 6, ALIGN_RIGHT, e->targeted);
}

/* ---------------------------------------------------------------- ZType: targets and shooting */

static void set_multiplier_shown(int m) {
    timer_init(&multiplierTimer, 2);
    hasMultiplierTimer = 1;
    multiplierIndex = m - 2;
    sound_play(m == 2 ? SFX_MULTI_2 : SFX_MULTI_3);
}

static void enemy_target(Entity *e) {
    e->targeted = 1;
    currentTarget = e;
    ents_erase(e);
    ents_push(e);
    timer_init(&e->reticleTimer, 0.5f);
    e->hasReticle = 1;
    sound_play(SFX_TARGET);
}

static int enemy_is_hit_by(Entity *e, char letter) {
    char expected = (char)tolower((unsigned char)e->rem[0]);
    letter = (char)tolower((unsigned char)letter);
    if (expected == letter) {
        memmove(e->rem, e->rem + 1, strlen(e->rem));
        if (!e->rem[0]) {
            currentTarget = NULL;
            e->dead = 1;
        }
        return 1;
    }
    return 0;
}

void game_cancel_target(void) {
    if (zg.mode != MODE_GAME || menu_active()) return;
    if (currentTarget) {
        currentTarget->targeted = 0;
        currentTarget = NULL;
        sound_play(SFX_CANCEL);
    }
}

void game_spawn_emp(void) {
    if (zg.mode != MODE_GAME || menu_active() || !zg.player) return;
    if (zg.emps) {
        spawn_emp(zg.player->x + zg.player->sx / 2, zg.player->y + zg.player->sy / 2);
        zg.emps--;
    }
}

void game_key_letter(char c) {
    if (zg.mode != MODE_GAME || menu_active()) return;
    if ((unsigned char)c < 33) return;
    if (nbuffered < (int)sizeof(buffered)) buffered[nbuffered++] = (char)tolower((unsigned char)c);
}

static void shoot_letter(char letter) {
    timer_reset(&idleTimer);
    if (!currentTarget) return;
    Entity *target = currentTarget;
    if (enemy_is_hit_by(target, letter)) {
        player_shoot(zg.player, target);
        zg.score += zg.multiplier;
        zg.hits++;
        zg.streak++;
        if (zg.streak > zg.longestStreak) zg.longestStreak = zg.streak;
        if (zg.streak == 20 || zg.streak == 50) {
            zg.multiplier = zg.streak == 20 ? 2 : 3;
            set_multiplier_shown(zg.multiplier);
        }
        if (target->dead) zg.kills++;
    } else {
        player_miss(zg.player);
        zg.multiplier = 1;
        zg.streak = 0;
        zg.misses++;
    }
}

static void shoot_buffered_letters(void) {
    while (nbuffered > 0) {
        char letter = buffered[0];
        memmove(buffered, buffered + 1, --nbuffered);
        if (!currentTarget) {
            Entity *pt[MAX_ENTS];
            int n = 0;
            for (int i = 0; i < nents; i++) {
                Entity *e = ents[i];
                if (e->type >= ENT_MINE && e->type <= ENT_BULLET && !e->killed && !e->targeted &&
                    tolower((unsigned char)e->rem[0]) == letter)
                    pt[n++] = e;
            }
            Entity *best = NULL;
            float nearest = -1;
            if (n == 1) {
                best = pt[0];
            } else if (n > 1) {
                for (int i = 0; i < nbuffered; i++) {
                    Entity *rp[MAX_ENTS];
                    int rn = 0;
                    for (int j = 0; j < n; j++)
                        if (strlen(pt[j]->rem) > (size_t)(i + 1) && tolower((unsigned char)pt[j]->rem[i + 1]) == buffered[i])
                            rp[rn++] = pt[j];
                    if (rn > 0) { memcpy(pt, rp, rn * sizeof(pt[0])); n = rn; }
                    else break;
                    if (n == 1) break;
                }
                for (int i = 0; i < n; i++) {
                    float d = distance_to(zg.player, pt[i]);
                    if (d < nearest || !best) { nearest = d; best = pt[i]; }
                }
            }
            if (best) {
                enemy_target(best);
            } else {
                player_miss(zg.player);
                zg.multiplier = 1;
                zg.streak = 0;
                zg.misses++;
            }
        }
        shoot_letter(letter);
    }
}

/* ---------------------------------------------------------------- ZType: waves */

static void wordlist_shuffle(void) {
    memcpy(wl_queue, wl, nwl * sizeof(wl[0]));
    nwl_queue = nwl;
    for (int i = nwl_queue - 1; i > 0; i--) {
        int j = (int)(frand() * (i + 1));
        const char *t = wl_queue[i];
        wl_queue[i] = wl_queue[j];
        wl_queue[j] = t;
    }
}

static const char *next_wordlist_word(void) {
    if (!nwl_queue) wordlist_shuffle();
    return wl_queue[--nwl_queue];
}

static void next_wave(void) {
    W.wave++;
    W.spawnWait = flimit(W.spawnWait * 0.97f, 0.2f, 1);
    W.currentSpawnWait = W.spawnWait;
    W.nspawn = 0;
    speedFactor *= W.speedIncrease;

    int dec = 0;
    for (int t = 0; t < 3; t++) {
        WaveType *ty = &W.types[t];
        ty->count -= dec;
        if (W.wave % ty->incEvery == 0) { ty->count++; dec++; }
        for (int s = 0; s < ty->count && W.nspawn < 160; s++) {
            SpawnItem *it = &W.spawn[W.nspawn++];
            it->kind = ty->kind;
            it->wait = 0;
            it->word = nwl ? next_wordlist_word() : NULL;
        }
    }
    for (int i = W.nspawn - 1; i > 0; i--) {       /* spawn.sort(random) */
        int j = (int)(frand() * (i + 1));
        SpawnItem tmp = W.spawn[i];
        W.spawn[i] = W.spawn[j];
        W.spawn[j] = tmp;
    }
}

static void spawn_current_wave(void) {
    if (!W.nspawn) {
        if (nents <= 1 && !hasWaveEnd) {
            timer_init(&waveEndTimer, 2);
            hasWaveEnd = 1;
        } else if (hasWaveEnd && timer_delta(&waveEndTimer) > 0) {
            hasWaveEnd = 0;
            next_wave();
        }
    } else if (timer_delta(&spawnTimer) > W.currentSpawnWait) {
        timer_reset(&spawnTimer);
        SpawnItem it = W.spawn[--W.nspawn];
        float x = fmap(frand(), 0, 1, 10, SYS_W - 10);
        spawn_enemy(it.kind, x, -30, it.word, 0, 1);
        W.currentSpawnWait = it.wait ? W.spawnWait * it.wait : W.spawnWait;
    }
}

/* ---------------------------------------------------------------- ZType: state changes */

static void game_reset(void) {
    memset(pool, 0, sizeof(pool));
    nents = 0;
    ndeferred = 0;
    currentTarget = NULL;
    nbuffered = 0;
    memset(&W, 0, sizeof(W));
    W.spawnWait = 0.7f;
    W.speedIncrease = 1.05f;
    W.types[0] = (WaveType){ENT_OPPRESSOR, 0, 7};
    W.types[1] = (WaveType){ENT_DESTROYER, 0, 3};
    W.types[2] = (WaveType){ENT_MINE, 3, 1};
    zg.score = 0;
    zg.streak = 0;
    zg.longestStreak = 0;
    zg.hits = 0;
    zg.misses = 0;
    zg.kills = 0;
    zg.multiplier = 1;
    zg.isPersonalBest = 0;
    zg.wave = 0;
    speedFactor = 1;
    timer_init(&lastKillTimer, 0);
    timer_init(&spawnTimer, 0);
    timer_init(&idleTimer, 0);
    hasWaveEnd = 0;
    hasMultiplierTimer = 0;
    nwl_queue = 0;
    zg.player = NULL;
}

void game_set_title(void) {
    game_reset();
    zg.mode = MODE_TITLE;
    menu_open_title();
    zg.emps = 0;
    zg.has_transition = 0;
}

static void game_set_game_state(void) {
    game_reset();
    Entity *p = ent_alloc();
    p->type = ENT_PLAYER;
    p->x = SYS_W / 2 - 6;
    p->y = PLAYER_SY;
    p->sx = p->sy = 8;
    p->ox = p->oy = 8;
    p->sheet = &SPR_SHIP;
    p->tw = p->th = 24;
    p->ety = TYPE_A;
    anim_init(&p->anims[0], SEQ_IDLE, 1, 60, 0);
    anim_init(&p->anims[1], SEQ_SHOOT, 4, 0.05f, 1);
    anim_init(&p->anims[2], SEQ_MISS, 3, 0.05f, 1);
    ents_push(p);
    zg.player = p;
    zg.mode = MODE_GAME;
    music_next();
    sound_play(SFX_SPAWN);
    zg.emps = 3;
}

void game_set_game(void) {
    game_set_game_state();
    timer_init(&zg.transition, 2);
    zg.has_transition = 1;
    next_wave();
    zg.wave = W.wave;
}

void game_set_game_over(void) {
    if (zg.score > zg.personalBest) {
        zg.isPersonalBest = 1;
        zg.personalBest = zg.score;
        stats_set_personal_best(zg.personalBest);
    }
    zg.mode = MODE_GAME_OVER;
    music_fade_out(1);
}

void game_show_game_over(void) {
    stats_add_game(zg.score, zg.wave, zg.longestStreak,
                   zg.hits ? (float)zg.hits / (zg.hits + zg.misses) * 100.0f : 0.0f);
    menu_open_game_over();
}

/* ---------------------------------------------------------------- ZType: update */

static void check_entities(void) {
    Entity *p = zg.player;
    if (!p || p->killed) return;
    for (int i = 0; i < nents; i++) {
        Entity *e = ents[i];
        if (e->type >= ENT_MINE && e->type <= ENT_BULLET && !e->killed && e->checkAgainst == TYPE_A &&
            p->ety == TYPE_A && ent_touches(e, p)) {
            player_kill(p);              /* other.kill() */
            enemy_kill(e, 0);            /* this.kill() */
            return;
        }
    }
}

static void game_update_entities(void) {
    for (int i = 0; i < nents; i++) {
        Entity *e = ents[i];
        if (!e->killed) ent_update(e);
    }
    check_entities();
    for (int i = 0; i < ndeferred; i++) {
        ents_erase(deferred[i]);
        deferred[i]->used = 0;
    }
    ndeferred = 0;
}

void game_update(void) {
    if (zt_in.menu) {
        if (menu_kind() == MENU_SETTINGS) {
            menu_close();
            if (zg.mode == MODE_TITLE) game_set_title();
        } else if (!menu_active()) menu_open_settings();
    }
    if (menu_active()) {
        zg.yScroll -= 100 * zt_tick;      /* backgroundMaps[0].scroll.y -= 100 * tick */
        menu_update();
        if (menu_kind() != MENU_GAMEOVER) return;
    }

    game_update_entities();

    if (zg.mode == MODE_GAME) {
        shoot_buffered_letters();
        spawn_current_wave();
        if (!menu_active() && zt_in.click && zt_in.mouse_x < 64 && zt_in.mouse_y < 64) menu_open_settings();
    } else if (zt_in.ok) {
        if (zg.mode == MODE_TITLE) game_set_game();
        else if ((zg.mode == MODE_GAME_OVER && menu_active() && menu_game_over_age() > 1.5f) || zg.mode != MODE_GAME_OVER)
            game_set_title();
    }

    float scrollSpeed = 100;
    if (hasWaveEnd) {
        if (zg.player) zg.player->targetAngle = 0;
        float dt = sinf(timer_delta(&waveEndTimer) * -0.5f * ZT_PI);
        scrollSpeed = 100 + dt * dt * 300;
        timer_reset(&idleTimer);
    }
    zg.yScroll2 += zt_tick * scrollSpeed * 0.1f;
    zg.yScroll2 = fmodf(zg.yScroll2, (float)STARS_SRC_H);
    zg.yScroll -= scrollSpeed * zt_tick;

    screenShakeAmount /= 1.1f;
    if (screenShakeAmount < 0.5f) screenShakeAmount = 0;
    zg.rscreen_x = frand() * screenShakeAmount;
    zg.rscreen_y = frand() * screenShakeAmount;

    zg.wave = W.wave;
}

/* ---------------------------------------------------------------- ZType: drawing */

void game_draw_stars(float scroll) {
    /* stars.jpg is drawn at 0.75 scale: draw(0, scroll - h) and draw(0, scroll) */
    float h = STARS_SRC_H * 0.75f;
    gx_image_sized(TEX_STARS, 0, 0.75f * scroll - h, SYS_W, h);
    gx_image_sized(TEX_STARS, 0, 0.75f * scroll, SYS_W, h);
}

static void draw_grid(void) {
    /* ig.BackgroundMap drawTiled with a single 620px tile, repeating */
    float ts = GRID_TILE;
    float scrollY = zg.yScroll;
    float pxOffsetY = fmodf(scrollY, ts);
    float pxMinY = -pxOffsetY - ts, pxMaxY = SYS_H + ts - pxOffsetY;
    gx_bind(TEX_GRID);
    for (float pxY = pxMinY; pxY < pxMaxY; pxY += ts) {
        if (pxY + ts <= 0 || pxY >= SYS_H) continue;
        gx_image_sized(TEX_GRID, 0, pxY, ts, ts);
    }
}

static void draw_multiplier_bar(float x, float y) {
    float multiLength = 1 - (75.0f / (75.0f + zg.streak * 2));
    float width = 640;
    if (zg.streak > 0) gx_fill(x, y, width * multiLength, 2, 0x4DFED2);
    float shown = hasMultiplierTimer ? timer_delta(&multiplierTimer) : 0;
    if (shown < 0) {
        gx_alpha(flimit(fmap(shown, -2, 0, 2, 0), 0, 1));
        gx_spr_tile(&SPR_MULTI_INDICATOR, multiplierIndex, 32, 18, x + width * multiLength - 30, y - 22, 0, 0);
        gx_alpha(1);
    }
}

static void draw_keyboard(void) {
    /* ig.Keyboard.drawMinimal */
    gx_save();
    gx_alpha(1);
    gx_scale(KB_DRAW_SCALE, KB_DRAW_SCALE);
    draw_multiplier_bar(0, KB_Y + 62);
    if (!zg.emps) gx_alpha(0.7f);
    gx_spr_tile(&SPR_EMP_BUTTONS, zg.emps, 34, 40, 582, KB_Y, 0, 0);
    gx_alpha(0.1f);
    gx_spr(&SPR_PAUSE, 28, 28);
    gx_restore();
}

static void draw_ui(void) {
    if (hasWaveEnd) {
        float d = -timer_delta(&waveEndTimer);
        float a = d > 1.7f ? fmap(d, 2, 1.7f, 0, 1) : d < 1 ? fmap(d, 1, 0, 1, 0) : 1;
        float ys = 276 + (d < 1 ? fmap(cosf(1 - d), 1, 0, 0, 250) : 0);
        char buf[48];
        gx_alpha(a);
        snprintf(buf, sizeof(buf), "WAVE %03d CLEAR", W.wave);
        ui_text(FONT_36_BLUE, buf, 32, ys, ALIGN_LEFT);
        gx_fill(32, ys + 48, 276, 2, 0x4DFED2);
        snprintf(buf, sizeof(buf), "SCORE: %06d", zg.score);
        ui_text(FONT_18_WHITE, buf, 32, ys * 1.2f + 10, ALIGN_LEFT);
        gx_alpha(1);
    }
    if (timer_delta(&idleTimer) > 8) {
        float d = timer_delta(&idleTimer);
        float aa = flimit(fmap(d, 8, 9, 0, 1), 0, 1);
        gx_alpha((sinf(d * 4) * 0.25f + 0.75f) * aa);
        ui_text(FONT_18_WHITE, "Type the words to shoot!\nENTER for EMP", SYS_W / 2, SYS_H - 180, ALIGN_CENTER);
        gx_alpha(1);
    }
    draw_keyboard();
}

static void draw_game(void) {
    game_draw_stars(zg.yScroll2);

    gx_alpha(0.8f);
    gx_image_sized(TEX_GRADIENT, 0, 0, SYS_W, SYS_H);

    float d = timer_delta(&lastKillTimer);
    gx_alpha(d < 0 ? d * -1 + 0.1f : 0.1f);
    draw_grid();
    gx_alpha(1);

    gx_lighter(1);
    for (int i = 0; i < nents; i++) ent_draw(ents[i]);
    gx_lighter(0);

    for (int i = 0; i < nents; i++) {
        const Entity *e = ents[i];
        if (e->type >= ENT_MINE && e->type <= ENT_BULLET) enemy_draw_label(e);
    }
    if (zg.mode == MODE_GAME) draw_ui();
}

void game_draw(void) {
    if (zg.mode == MODE_GAME || zg.mode == MODE_GAME_OVER) draw_game();
    if (menu_active()) {
        menu_draw();
        int has;
        float s = menu_scroll(&has);
        if (has) zg.yScroll2 = s;
        if (zg.has_transition) {
            float dt = 2 - (timer_delta(&zg.transition) * -1);
            /* ig.ease.inOutBack(dt, 400, move, 2): the ship slides from the title position to the game position */
            float t = dt / 1.0f, c = PLAYER_SY - 400, sv = 1.70158f, v;
            if (t < 1) { sv *= 1.525f; v = c / 2 * (t * t * ((sv + 1) * t - sv)) + 400; }
            else { t -= 2; sv *= 1.525f; v = c / 2 * (t * t * ((sv + 1) * t + sv) + 2) + 400; }
            menu_set_transition(dt / 2, 1 - dt / 2, zg.player ? &v : NULL);
            if (zg.player) zg.player->y = v;
            if (dt > 2) {
                zg.has_transition = 0;
                menu_close();
            }
        }
    }
}

/* ---------------------------------------------------------------- init, custom text */

void game_init(void) {
    memset(&zg, 0, sizeof(zg));
    zg.personalBest = stats_personal_best();
    game_set_title();
}

#ifdef ZT_TEST_SCRIPT
char game_test_next_letter(void) {
    Entity *t = currentTarget;
    if (t && (t->killed || t->dead || !t->rem[0])) t = NULL;
    for (int i = 0; !t && i < nents; i++) {
        Entity *e = ents[i];
        if (e->type >= ENT_MINE && e->type <= ENT_BULLET && !e->killed && !e->dead && e->rem[0] &&
            (!t || e->y > t->y))
            t = e;
    }
    return t ? t->rem[0] : 0;
}
#endif

int game_has_custom_words(void) { return nwl > 0; }

int game_load_words(const char *text) {
    /* ZType.extractWords(text, true): words are runs of letters; apostrophes are dropped */
    size_t len = strlen(text);
    char *buf = malloc(len + 2);
    if (!buf) return 0;
    const char *list[2048];
    int n = 0, inword = 0;
    size_t o = 0, start = 0;
    for (const char *p = text;; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalpha(c)) {
            if (!inword) { inword = 1; start = o; }
            buf[o++] = (char)c;
        } else if (c == '\'' && inword) {
            /* "don't" -> "dont" */
        } else {
            if (inword) {
                buf[o++] = 0;
                inword = 0;
                if (o - start - 1 <= 15 && n < 2048) list[n++] = buf + start;
            }
            if (!c) break;
        }
    }
    if (n < 8) {
        free(buf);
        return 0;
    }
    free(wl_text);
    wl_text = buf;
    nwl = n;
    memcpy(wl, list, n * sizeof(list[0]));
    nwl_queue = 0;
    return n;
}
