/*
 * ZType for Nintendo Wii
 * Visual and mechanical 1:1 reproduction of Dominic Szablewski's ZType (zty.pe).
 */
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <wiikeyboard/keyboard.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fat.h>
#include <sys/stat.h>
#include <math.h>

#include "gx_render.h"
#include "assets.h"
#include "words.h"
#include "sound.h"

#define HS_DIR "sd:/apps/typeshot"
#define HS_FILE HS_DIR "/highscore.dat"
static int sdOK = 0;
static int hiscore = 0;

static unsigned hsSum(unsigned v) { return (v * 2654435761u) ^ 0x5459u; }
static void hsLoad(void) {
    if (!sdOK) return;
    FILE *f = fopen(HS_FILE, "r");
    if (!f) return;
    unsigned v, c;
    if (fscanf(f, "TSHOT1 %u %u", &v, &c) == 2 && c == hsSum(v)) hiscore = (int)v;
    fclose(f);
}
static void hsSave(void) {
    if (!sdOK) return;
    mkdir("sd:/apps", 0777);
    mkdir(HS_DIR, 0777);
    FILE *f = fopen(HS_FILE, "w");
    if (!f) return;
    fprintf(f, "TSHOT1 %u %u\n", (unsigned)hiscore, hsSum((unsigned)hiscore));
    fclose(f);
}

/* ---------------- Easing functions ---------------- */
static float inOutQuad(float t, float b, float c, float d) {
    t /= d / 2.0f;
    if (t < 1.0f) return c / 2.0f * t * t + b;
    t -= 1.0f;
    return -c / 2.0f * (t * (t - 2.0f) - 1.0f) + b;
}

static float inOutBack(float t, float b, float c, float d) {
    float s = 1.70158f;
    t /= d / 2.0f;
    if (t < 1.0f) {
        s *= 1.525f;
        return c / 2.0f * (t * t * ((s + 1.0f) * t - s)) + b;
    }
    t -= 2.0f;
    s *= 1.525f;
    return c / 2.0f * (t * t * ((s + 1.0f) * t + s) + 2.0f) + b;
}

/* ---------------- Game state & entities ---------------- */
#define MAX_ENEMIES 32
#define MAX_BULLETS 48
#define MAX_SPARKS  256
#define MAX_EXPLOSIONS 16
#define MAX_ENEMY_BULLETS 32

enum {
    ENEMY_MINE = 0,
    ENEMY_DESTROYER,
    ENEMY_OPPRESSOR,
    ENEMY_BULLET
};

typedef struct {
    int active;
    int type;
    char word[24];
    char remaining[24];
    int len;
    float x, y;
    float vx, vy;
    float angle;
    float spin;
    float shootTimer;
    int targeted;
    float reticleTimer;
    int dying;
    int inflight;
} Enemy;

typedef struct {
    int active;
    float x, y;
    int target_enemy;
    float angle;
} Plasma;

typedef struct {
    int active;
    float x, y;
    float vx, vy;
    float life, maxLife;
    u32 color;
} Spark;

typedef struct {
    int active;
    float x, y;
    float frame;
    float scale;
} Explosion;

typedef struct {
    int active;
    float x, y;
    float radius;
    float alpha;
} EMPWave;

enum {
    STATE_TITLE,
    STATE_INTRO_TRANSITION,
    STATE_PLAY,
    STATE_PAUSE,
    STATE_OVER
};

static int state = STATE_TITLE;
static float titleTransitionTime = 0.0f;

static int wave = 0;
static int score = 0;
static int streak = 0;
static int longestStreak = 0;
static int hits = 0;
static int misses = 0;
static int kills = 0;
static int multiplier = 1;
static int emps = 3;
static int isPersonalBest = 0;
static float multiplierTimer = 0.0f;
static int multiplierIndex = 0;

static float idleTimer = 0.0f;
static float waveEndTimer = 0.0f;
static float spawnTimer = 0.0f;
static float spawnWait = 0.7f;
static float speedFactor = 1.0f;

static float player_x = 234.0f;
static float player_y = 642.0f;
static float player_angle = 0.0f;
static float player_target_angle = 0.0f;
static int player_anim_frame = 0;
static int player_anim_timer = 0;

static float bg_stars_scroll = 0.0f;
static float lastKillTimer = 0.0f;
static int screen_shake = 0;
static int currentTargetIdx = -1;

static Enemy enemies[MAX_ENEMIES];
static Plasma plasmas[MAX_BULLETS];
static Spark sparks[MAX_SPARKS];
static Explosion explosions[MAX_EXPLOSIONS];
static EMPWave empWave;

typedef struct {
    int type;
    int wait;
} WaveSpawnItem;
static WaveSpawnItem waveSpawnQueue[64];
static int waveSpawnCount = 0;

static unsigned rng_seed = 123456789;
static int rnd(int max) {
    if (max <= 0) return 0;
    rng_seed = rng_seed * 1103515245 + 12345;
    return (int)((rng_seed >> 16) % (unsigned)max);
}
static float rnd_range(float min, float max) {
    return min + (rnd(10000) / 10000.0f) * (max - min);
}

/* ---------------- Particle helpers ---------------- */
static void spawnSparks(float x, float y, int count, u32 color, float speed) {
    for (int i = 0; i < count; i++) {
        for (int k = 0; k < MAX_SPARKS; k++) {
            if (!sparks[k].active) {
                float a = rnd_range(0.0f, 6.283185f);
                float sp = rnd_range(speed * 0.4f, speed * 1.5f);
                sparks[k].active = 1;
                sparks[k].x = x;
                sparks[k].y = y;
                sparks[k].vx = cosf(a) * sp;
                sparks[k].vy = sinf(a) * sp;
                sparks[k].life = sparks[k].maxLife = rnd_range(15.0f, 30.0f);
                sparks[k].color = color;
                break;
            }
        }
    }
}

static void spawnExplosion(float x, float y, float scale) {
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (!explosions[i].active) {
            explosions[i].active = 1;
            explosions[i].x = x;
            explosions[i].y = y;
            explosions[i].frame = 0.0f;
            explosions[i].scale = scale;
            break;
        }
    }
}

static void triggerScreenShake(int amount) {
    if (amount > screen_shake) screen_shake = amount;
}

/* ---------------- Words & Target selection ---------------- */
static int isLetterAlreadyTargeted(char c) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && !enemies[i].dying && enemies[i].remaining[0] == c) {
            return 1;
        }
    }
    return 0;
}

static const char *getWordWithLength(int minL, int maxL) {
    if (minL < 2) minL = 2;
    if (maxL > 12) maxL = 12;

    for (int tries = 0; tries < 50; tries++) {
        int l = minL + rnd(maxL - minL + 1);
        int count = WORDS_COUNT_BY_LEN[l];
        if (count > 0) {
            const char *w = WORDS_BY_LEN[l][rnd(count)];
            if (!isLetterAlreadyTargeted(w[0]) || tries > 30) {
                return w;
            }
        }
    }
    return "ztype";
}

/* ---------------- Enemy spawning ---------------- */
static void spawnEnemyEntity(int type, float x, float y, const char *forced_word) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            Enemy *e = &enemies[i];
            memset(e, 0, sizeof(Enemy));
            e->active = 1;
            e->type = type;
            e->x = x;
            e->y = y;
            e->reticleTimer = 0.0f;
            e->targeted = 0;

            if (type == ENEMY_MINE) {
                const char *w = forced_word ? forced_word : getWordWithLength(3, 6);
                strncpy(e->word, w, sizeof(e->word) - 1);
                strcpy(e->remaining, e->word);
                e->len = strlen(e->word);
                float speed = (30.0f / 60.0f) * speedFactor;
                float angle = atan2f(player_y - y, player_x - x);
                e->vx = cosf(angle) * speed;
                e->vy = sinf(angle) * speed;
                e->angle = 0.0f;
                e->spin = 2.0f / 60.0f;
            } else if (type == ENEMY_DESTROYER) {
                const char *w = forced_word ? forced_word : getWordWithLength(7, 10);
                strncpy(e->word, w, sizeof(e->word) - 1);
                strcpy(e->remaining, e->word);
                e->len = strlen(e->word);
                float speed = (20.0f / 60.0f) * speedFactor;
                float deg = rnd_range(67.0f, 90.0f) + (x > GAME_W / 2 ? 22.5f : 0.0f);
                float angle = deg * (3.14159265f / 180.0f);
                e->vx = cosf(angle) * speed;
                e->vy = sinf(angle) * speed;
                e->angle = angle - 1.570796f;
                e->shootTimer = 5.0f;
            } else if (type == ENEMY_OPPRESSOR) {
                const char *w = forced_word ? forced_word : getWordWithLength(9, 12);
                strncpy(e->word, w, sizeof(e->word) - 1);
                strcpy(e->remaining, e->word);
                e->len = strlen(e->word);
                float speed = (15.0f / 60.0f) * speedFactor;
                e->vx = 0.0f;
                e->vy = speed;
                e->angle = 0.0f;
                e->shootTimer = 7.0f;
            } else if (type == ENEMY_BULLET) {
                char single[2] = { (char)('a' + rnd(26)), '\0' };
                strncpy(e->word, single, sizeof(e->word) - 1);
                strcpy(e->remaining, e->word);
                e->len = 1;
                // vx, vy set by caller
            }
            return;
        }
    }
}

static void nextWave(void) {
    wave++;
    spawnWait = spawnWait * 0.97f;
    if (spawnWait < 0.2f) spawnWait = 0.2f;
    speedFactor *= 1.05f;

    waveSpawnCount = 0;

    int mineCount = 3 + (wave / 1);
    int destroyerCount = wave / 3;
    int oppressorCount = wave / 7;

    mineCount -= oppressorCount;
    if (mineCount < 1) mineCount = 1;

    for (int i = 0; i < oppressorCount && waveSpawnCount < 60; i++) {
        waveSpawnQueue[waveSpawnCount++].type = ENEMY_OPPRESSOR;
    }
    for (int i = 0; i < destroyerCount && waveSpawnCount < 60; i++) {
        waveSpawnQueue[waveSpawnCount++].type = ENEMY_DESTROYER;
    }
    for (int i = 0; i < mineCount && waveSpawnCount < 60; i++) {
        waveSpawnQueue[waveSpawnCount++].type = ENEMY_MINE;
    }

    // Shuffle wave spawn queue
    for (int i = waveSpawnCount - 1; i > 0; i--) {
        int j = rnd(i + 1);
        WaveSpawnItem tmp = waveSpawnQueue[i];
        waveSpawnQueue[i] = waveSpawnQueue[j];
        waveSpawnQueue[j] = tmp;
    }

    spawnTimer = 0.0f;
    waveEndTimer = 0.0f;
}

/* ---------------- EMP trigger ---------------- */
static void triggerEMP(void) {
    if (emps <= 0) return;
    emps--;
    sound_play(SFX_EMP);
    triggerScreenShake(30);

    empWave.active = 1;
    empWave.x = player_x + 12.0f;
    empWave.y = player_y + 12.0f;
    empWave.radius = 0.0f;
    empWave.alpha = 1.0f;

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && !enemies[i].dying) {
            spawnSparks(enemies[i].x, enemies[i].y, 15, 0x4DFED2FF, 4.0f);
            spawnExplosion(enemies[i].x, enemies[i].y, 1.2f);
            enemies[i].dying = 1;
            enemies[i].active = 0;
            kills++;
        }
    }
    currentTargetIdx = -1;
}

/* ---------------- Shooting & Input handling ---------------- */
static void shootPlasma(int enemyIdx) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!plasmas[i].active) {
            plasmas[i].active = 1;
            plasmas[i].x = player_x + 12.0f;
            plasmas[i].y = player_y + 8.0f;
            plasmas[i].target_enemy = enemyIdx;
            enemies[enemyIdx].inflight++;

            float dx = enemies[enemyIdx].x - (player_x + 12.0f);
            float dy = enemies[enemyIdx].y - (player_y + 8.0f);
            plasmas[i].angle = atan2f(dy, dx) + 1.570796f;

            sound_play(SFX_PLASMA);
            player_anim_frame = 3;
            player_anim_timer = 4;
            return;
        }
    }
}

static void handleKey(char c) {
    if (state != STATE_PLAY) return;
    idleTimer = 0.0f;

    if (c >= 'A' && c <= 'Z') c += 32;
    if (c < 'a' || c > 'z') return;

    // 1. If no current target, acquire target
    if (currentTargetIdx < 0) {
        float lowest_y = -9999.0f;
        int best = -1;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (enemies[i].active && !enemies[i].dying && enemies[i].remaining[0] == c) {
                if (enemies[i].y > lowest_y) {
                    lowest_y = enemies[i].y;
                    best = i;
                }
            }
        }
        if (best >= 0) {
            currentTargetIdx = best;
            enemies[best].targeted = 1;
            enemies[best].reticleTimer = 0.5f;
            sound_play(SFX_TARGET);
        }
    }

    // 2. Check hit on current target
    if (currentTargetIdx >= 0 && enemies[currentTargetIdx].active) {
        Enemy *e = &enemies[currentTargetIdx];
        if (e->remaining[0] == c) {
            // Hit!
            memmove(e->remaining, e->remaining + 1, strlen(e->remaining));
            hits++;
            streak++;
            if (streak > longestStreak) longestStreak = streak;

            int oldMul = multiplier;
            if (streak >= 50) multiplier = 3;
            else if (streak >= 20) multiplier = 2;
            else multiplier = 1;

            if (multiplier > oldMul) {
                multiplierTimer = 2.0f;
                multiplierIndex = multiplier - 2;
                if (multiplier == 2) sound_play(SFX_MULTI_2);
                else if (multiplier == 3) sound_play(SFX_MULTI_3);
            }

            score += multiplier;

            float dx = e->x - (player_x + 12.0f);
            float dy = e->y - (player_y + 12.0f);
            player_target_angle = atan2f(dx, -dy);

            shootPlasma(currentTargetIdx);

            if (e->remaining[0] == '\0') {
                e->dying = 1;
                currentTargetIdx = -1;
            }
            return;
        }
    }

    // Miss!
    misses++;
    streak = 0;
    multiplier = 1;
    sound_play(SFX_CLICK);
    player_anim_frame = 4;
    player_anim_timer = 6;
    spawnSparks(player_x + 12.0f, player_y + 4.0f, 6, 0xFF4444FF, 2.0f);
}

/* ---------------- Player Death ---------------- */
static void killPlayer(void) {
    state = STATE_OVER;
    sound_play(SFX_EXPLODE_P);
    spawnExplosion(player_x + 12.0f, player_y + 12.0f, 2.8f);
    spawnSparks(player_x + 12.0f, player_y + 12.0f, 60, 0x4DFED2FF, 6.0f);
    triggerScreenShake(25);

    if (score > hiscore) {
        hiscore = score;
        isPersonalBest = 1;
        hsSave();
    } else {
        isPersonalBest = 0;
    }
}

/* ---------------- Enemy kill ---------------- */
static void killEnemy(int i) {
    Enemy *e = &enemies[i];
    e->active = 0;
    kills++;
    lastKillTimer = 0.3f;

    if (e->type == ENEMY_MINE) {
        sound_play(SFX_EXPLODE_M);
        spawnExplosion(e->x, e->y, 1.1f);
        spawnSparks(e->x, e->y, 20, 0xFFCC44FF, 3.5f);
        triggerScreenShake(5);
    } else if (e->type == ENEMY_DESTROYER) {
        sound_play(SFX_EXPLODE_L);
        spawnExplosion(e->x, e->y, 1.8f);
        spawnSparks(e->x, e->y, 40, 0xFF8844FF, 4.5f);
        triggerScreenShake(10);
    } else if (e->type == ENEMY_OPPRESSOR) {
        sound_play(SFX_EXPLODE_L);
        spawnExplosion(e->x, e->y, 2.2f);
        spawnSparks(e->x, e->y, 50, 0xFF4422FF, 5.0f);
        triggerScreenShake(20);
    } else if (e->type == ENEMY_BULLET) {
        sound_play(SFX_EXPLODE_S);
        spawnExplosion(e->x, e->y, 0.8f);
        spawnSparks(e->x, e->y, 10, 0x55FFAAFF, 2.5f);
    }

    if (currentTargetIdx == i) currentTargetIdx = -1;
}

/* ---------------- Game state initialization ---------------- */
static void setGame(void) {
    state = STATE_INTRO_TRANSITION;
    titleTransitionTime = 0.0f;
    sound_play(SFX_SPAWN);

    memset(enemies, 0, sizeof(enemies));
    memset(plasmas, 0, sizeof(plasmas));
    memset(sparks, 0, sizeof(sparks));
    memset(explosions, 0, sizeof(explosions));
    memset(&empWave, 0, sizeof(empWave));

    wave = 0;
    score = 0;
    streak = 0;
    longestStreak = 0;
    hits = 0;
    misses = 0;
    kills = 0;
    multiplier = 1;
    emps = 3;
    isPersonalBest = 0;
    multiplierTimer = 0.0f;
    multiplierIndex = 0;
    idleTimer = 0.0f;
    waveEndTimer = 0.0f;
    spawnWait = 0.7f;
    speedFactor = 1.0f;

    player_x = 234.0f;
    player_y = 400.0f;
    player_angle = 0.0f;
    player_target_angle = 0.0f;
    currentTargetIdx = -1;

    nextWave();
}

/* ---------------- Update ---------------- */
static void update(void) {
    // Scrolling starfield
    bg_stars_scroll += 10.0f / 60.0f;
    if (bg_stars_scroll >= 512.0f) bg_stars_scroll -= 512.0f;

    if (screen_shake > 0) screen_shake--;
    if (lastKillTimer > 0.0f) lastKillTimer -= 1.0f / 60.0f;
    if (multiplierTimer > 0.0f) multiplierTimer -= 1.0f / 60.0f;

    // Sparks update
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (sparks[i].active) {
            sparks[i].x += sparks[i].vx;
            sparks[i].y += sparks[i].vy;
            sparks[i].vx *= 0.96f;
            sparks[i].vy *= 0.96f;
            if (--sparks[i].life <= 0) sparks[i].active = 0;
        }
    }

    // Explosions update
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (explosions[i].active) {
            explosions[i].frame += 0.25f;
            if (explosions[i].frame >= 3.0f) explosions[i].active = 0;
        }
    }

    // EMP wave update
    if (empWave.active) {
        empWave.radius += 12.0f;
        empWave.alpha = 1.0f - (empWave.radius / 300.0f);
        if (empWave.radius >= 300.0f) empWave.active = 0;
    }

    if (state == STATE_INTRO_TRANSITION) {
        titleTransitionTime += 1.0f / 60.0f;
        if (titleTransitionTime >= 2.0f) {
            titleTransitionTime = 2.0f;
            player_y = 642.0f;
            state = STATE_PLAY;
        } else {
            player_y = inOutBack(titleTransitionTime, 400.0f, 242.0f, 2.0f);
        }
        return;
    }

    if (state != STATE_PLAY) return;

    idleTimer += 1.0f / 60.0f;

    // Player animation timer
    if (player_anim_timer > 0) {
        if (--player_anim_timer == 0) {
            if (player_anim_frame >= 4) player_anim_frame = 0;
            else if (player_anim_frame > 0) {
                player_anim_frame--;
                if (player_anim_frame > 0) player_anim_timer = 3;
            }
        }
    }

    // Slerp player angle
    float dAngle = player_target_angle - player_angle;
    player_angle += dAngle * 0.18f;

    // Spawn queue processing
    if (waveSpawnCount > 0) {
        spawnTimer += 1.0f / 60.0f;
        if (spawnTimer >= spawnWait) {
            spawnTimer = 0.0f;
            WaveSpawnItem item = waveSpawnQueue[--waveSpawnCount];
            float x = rnd_range(20.0f, GAME_W - 20.0f);
            spawnEnemyEntity(item.type, x, -30.0f, NULL);
        }
    }

    // Update enemies
    int aliveCount = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            Enemy *e = &enemies[i];
            aliveCount++;

            e->x += e->vx;
            e->y += e->vy;
            e->angle += e->spin;

            if (e->reticleTimer > 0.0f) {
                e->reticleTimer -= 1.0f / 60.0f;
                if (e->reticleTimer < 0.0f) e->reticleTimer = 0.0f;
            }

            // Oppressor / Destroyer shooting
            if (!e->dying && e->y > 30.0f) {
                e->shootTimer -= 1.0f / 60.0f;
                if (e->shootTimer <= 0.0f) {
                    if (e->type == ENEMY_DESTROYER) {
                        e->shootTimer = 5.0f;
                        spawnEnemyEntity(ENEMY_BULLET, e->x, e->y + 20.0f, NULL);
                    } else if (e->type == ENEMY_OPPRESSOR) {
                        e->shootTimer = 7.0f;
                        for (int b = 0; b < 8; b++) {
                            float deg = 40.0f + b * (100.0f / 7.0f);
                            float rad = deg * (3.14159265f / 180.0f);
                            float bx = e->x + cosf(rad) * 21.0f;
                            float by = e->y + 48.0f + sinf(rad) * 21.0f;
                            spawnEnemyEntity(ENEMY_BULLET, bx, by, NULL);
                        }
                    }
                }
            }

            // Check collision with player (One-hit death!)
            float pdx = (player_x + 12.0f) - e->x;
            float pdy = (player_y + 12.0f) - e->y;
            float pdist = sqrtf(pdx * pdx + pdy * pdy);
            if (pdist < 22.0f || e->y >= 680.0f) {
                killPlayer();
                return;
            }
        }
    }

    // Update plasmas
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (plasmas[i].active) {
            Plasma *p = &plasmas[i];
            int eIdx = p->target_enemy;
            if (!enemies[eIdx].active) {
                p->active = 0;
                continue;
            }

            float dx = enemies[eIdx].x - p->x;
            float dy = enemies[eIdx].y - p->y;
            float d = sqrtf(dx * dx + dy * dy);

            if (d < 16.0f) {
                p->active = 0;
                enemies[eIdx].inflight--;
                sound_play(SFX_HIT);
                spawnSparks(p->x, p->y, 6, 0x4DFED2FF, 2.5f);

                if (enemies[eIdx].dying && enemies[eIdx].inflight <= 0) {
                    killEnemy(eIdx);
                }
            } else {
                p->x += (dx / d) * (800.0f / 60.0f);
                p->y += (dy / d) * (800.0f / 60.0f);
            }
        }
    }

    // Wave clear check
    if (waveSpawnCount == 0 && aliveCount == 0) {
        if (waveEndTimer <= 0.0f) {
            waveEndTimer = 2.0f;
        } else {
            waveEndTimer -= 1.0f / 60.0f;
            if (waveEndTimer <= 0.0f) {
                nextWave();
            }
        }
    }
}

/* ---------------- Render ---------------- */
static void renderBackground(void) {
    // 1. Starfield
    gx_bind_stars();
    gx_draw_sprite(0, bg_stars_scroll - 512.0f, GAME_W, 512, 0, 0, 1, 1, 0xFFFFFFFF);
    gx_draw_sprite(0, bg_stars_scroll, GAME_W, 512, 0, 0, 1, 1, 0xFFFFFFFF);
    gx_draw_sprite(0, bg_stars_scroll + 512.0f, GAME_W, 512, 0, 0, 1, 1, 0xFFFFFFFF);

    // 2. Space gradient overlay (alpha 0.8)
    gx_draw_gradient(0, 0, GAME_W, GAME_H, 0x031214D0, 0x234442D0);

    // 3. Cyber grid with kill flash
    gx_bind_grid();
    gx_set_blend_additive();
    float alpha_f = (lastKillTimer > 0.0f) ? (lastKillTimer * 3.33f * 0.3f + 0.1f) : 0.1f;
    u8 grid_alpha = (u8)(alpha_f * 255.0f);
    gx_draw_sprite(0, 0, GAME_W, GAME_H, 0, 0, 1.0f, 1.5f, (0xFFFFFF00) | grid_alpha);
    gx_set_blend_alpha();
}

static void renderPlayer(void) {
    gx_set_blend_additive();
    float u0 = (SPRITE_SHIP_X + player_anim_frame * 24.0f) / 512.0f;
    float v0 = SPRITE_SHIP_V0;
    float u1 = (SPRITE_SHIP_X + (player_anim_frame + 1) * 24.0f) / 512.0f;
    float v1 = SPRITE_SHIP_V1;

    gx_draw_sprite_rot(player_x + 12.0f, player_y + 12.0f, 32, 32, player_angle,
                       u0, v0, u1, v1, 0xFFFFFFFF);
    gx_set_blend_alpha();
}

static void renderEnemies(void) {
    gx_bind_sprites();
    gx_set_blend_additive();

    // 1. Enemy sprites
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            Enemy *e = &enemies[i];
            if (e->type == ENEMY_MINE) {
                gx_draw_sprite_rot(e->x, e->y, 32, 32, e->angle,
                                   SPRITE_MINE_U0, SPRITE_MINE_V0,
                                   SPRITE_MINE_U1, SPRITE_MINE_V1,
                                   0xFFFFFFFF);
            } else if (e->type == ENEMY_DESTROYER) {
                gx_draw_sprite_rot(e->x, e->y, 43, 58, e->angle,
                                   SPRITE_DESTROYER_U0, SPRITE_DESTROYER_V0,
                                   SPRITE_DESTROYER_U1, SPRITE_DESTROYER_V1,
                                   0xFFFFFFFF);
            } else if (e->type == ENEMY_OPPRESSOR) {
                gx_draw_sprite(e->x - 34, e->y - 44, 68, 88,
                               SPRITE_OPPRESSOR_U0, SPRITE_OPPRESSOR_V0,
                               SPRITE_OPPRESSOR_U1, SPRITE_OPPRESSOR_V1,
                               0xFFFFFFFF);
            } else if (e->type == ENEMY_BULLET) {
                gx_draw_sprite(e->x - 10, e->y - 12, 20, 24,
                               SPRITE_BULLET_U0, SPRITE_BULLET_V0,
                               SPRITE_BULLET_U1, SPRITE_BULLET_V1,
                               0xFFAA33FF);
            }

            // Reticle animation (initial lock-on only)
            if (e->reticleTimer > 0.0f) {
                float d = (0.5f - e->reticleTimer) / 0.5f; // 0 -> 1
                float scale = (1.0f - d) * (1.0f - d) * 4.0f;
                float angle = d * 3.14159265f;
                float sz = 128.0f * scale;
                u8 ra = (u8)(sqrtf(d) * 255.0f);
                gx_draw_sprite_rot(e->x, e->y, sz, sz, angle,
                                   SPRITE_RETICLE_U0, SPRITE_RETICLE_V0,
                                   SPRITE_RETICLE_U1, SPRITE_RETICLE_V1,
                                   (0x4DFED200) | ra);
            }
        }
    }

    // 2. Plasmas
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (plasmas[i].active) {
            gx_draw_sprite_rot(plasmas[i].x, plasmas[i].y, 32, 32, plasmas[i].angle,
                               SPRITE_PLASMA_U0, SPRITE_PLASMA_V0,
                               SPRITE_PLASMA_U1, SPRITE_PLASMA_V1,
                               0xFFFFFFFF);
        }
    }

    // 3. Explosions
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (explosions[i].active) {
            int f = (int)explosions[i].frame;
            if (f > 2) f = 2;
            float u0 = (SPRITE_EXPLOSION_X + f * 32.0f) / 512.0f;
            float v0 = SPRITE_EXPLOSION_V0;
            float u1 = (SPRITE_EXPLOSION_X + (f + 1) * 32.0f) / 512.0f;
            float v1 = SPRITE_EXPLOSION_V1;
            float sz = 48.0f * explosions[i].scale;
            gx_draw_sprite(explosions[i].x - sz * 0.5f, explosions[i].y - sz * 0.5f, sz, sz,
                           u0, v0, u1, v1, 0xFFFFFFFF);
        }
    }

    // 4. Sparks
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (sparks[i].active) {
            float a = sparks[i].life / sparks[i].maxLife;
            u8 alpha = (u8)(a * 255.0f);
            u32 c = (sparks[i].color & 0xFFFFFF00) | alpha;
            float sz = 16.0f * a + 4.0f;
            gx_draw_sprite(sparks[i].x - sz * 0.5f, sparks[i].y - sz * 0.5f, sz, sz,
                           SPRITE_SPARK_U0, SPRITE_SPARK_V0,
                           SPRITE_SPARK_U1, SPRITE_SPARK_V1,
                           c);
        }
    }

    // 5. EMP wave
    if (empWave.active) {
        u8 a = (u8)(empWave.alpha * 255.0f);
        float sz = empWave.radius * 2.0f;
        gx_draw_sprite(empWave.x - empWave.radius, empWave.y - empWave.radius, sz, sz,
                       SPRITE_EMP_U0, SPRITE_EMP_V0,
                       SPRITE_EMP_U1, SPRITE_EMP_V1,
                       (0x4DFED200) | a);
    }

    gx_set_blend_alpha();

    // 6. Word labels
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && !enemies[i].dying && enemies[i].remaining[0] != '\0') {
            Enemy *e = &enemies[i];
            int w = gx_text_width_enemy(e->word);
            float x = e->x - 6.0f;
            if (x < (float)(w + 2)) x = (float)(w + 2);
            if (x > (float)(GAME_W - 1)) x = (float)(GAME_W - 1);

            float y = e->y + 16.0f;
            if (y < 2.0f) y = 2.0f;
            if (y > (float)(GAME_H - 19)) y = (float)(GAME_H - 19);

            float bx = x - w - 2.0f;
            float by = y - 1.0f;

            // Semi-transparent black box
            gx_draw_rect(bx, by + 5.0f, w + 8, 24, 0x000000C0);

            // Right-aligned word: white for untyped, orange for targeted
            gx_draw_text_enemy_right(x + 2.0f, y + 6.0f, e->remaining, e->targeted);
        }
    }
}

static void renderHUD(void) {
    gx_bind_sprites();

    // Blue streak progress bar along the bottom
    if (streak > 0) {
        float multiLength = 1.0f - (75.0f / (75.0f + streak * 2.0f));
        gx_draw_sprite(0, 718.0f, GAME_W * multiLength, 2.0f,
                       SPRITE_BAR_BLUE_U0, SPRITE_BAR_BLUE_V0,
                       SPRITE_BAR_BLUE_U1, SPRITE_BAR_BLUE_V1,
                       0x4DFED2FF);

        // Multiplier indicator (x2 / x3)
        if (multiplierTimer > 0.0f) {
            float a = multiplierTimer / 2.0f;
            if (a > 1.0f) a = 1.0f;
            u8 ma = (u8)(a * 255.0f);
            float mu0 = (SPRITE_MULTI_INDICATOR_X + multiplierIndex * 32.0f) / 512.0f;
            float mv0 = SPRITE_MULTI_INDICATOR_V0;
            float mu1 = (SPRITE_MULTI_INDICATOR_X + (multiplierIndex + 1) * 32.0f) / 512.0f;
            float mv1 = SPRITE_MULTI_INDICATOR_V1;

            gx_draw_sprite(GAME_W * multiLength - 22.5f, 701.5f, 24.0f, 14.0f,
                           mu0, mv0, mu1, mv1, (0xFFFFFF00) | ma);
        }
    }

    // EMP button icon at bottom right
    int empTile = emps;
    if (empTile < 0) empTile = 0;
    if (empTile > 3) empTile = 3;
    float eu0 = (SPRITE_EMP_BUTTONS_X + empTile * 34.0f) / 512.0f;
    float ev0 = SPRITE_EMP_BUTTONS_V0;
    float eu1 = (SPRITE_EMP_BUTTONS_X + (empTile + 1) * 34.0f) / 512.0f;
    float ev1 = SPRITE_EMP_BUTTONS_V1;
    u8 empAlpha = (emps > 0) ? 0xFF : 0xB2; // 0.7 alpha when 0
    gx_draw_sprite(436.5f, 672.0f, 26.0f, 30.0f, eu0, ev0, eu1, ev1, (0xFFFFFF00) | empAlpha);

    // Subtle pause icon at top-left (alpha 0.1)
    gx_draw_sprite(21.0f, 21.0f, 14.0f, 16.0f,
                   SPRITE_PAUSE_U0, SPRITE_PAUSE_V0,
                   SPRITE_PAUSE_U1, SPRITE_PAUSE_V1,
                   0xFFFFFF1A);

    // Wave clear banner (sliding and fading)
    if (waveEndTimer > 0.0f) {
        float d = 2.0f - waveEndTimer; // 0 -> 2
        float a = (d > 1.7f) ? (2.0f - d) / 0.3f : (d < 1.0f ? d : 1.0f);
        if (a < 0.0f) a = 0.0f;
        if (a > 1.0f) a = 1.0f;
        u8 alpha = (u8)(a * 255.0f);

        float ys = 276.0f + (d < 1.0f ? cosf(1.0f - d) * 250.0f : 0.0f);

        char wbuf[32];
        snprintf(wbuf, sizeof(wbuf), "WAVE %03d CLEAR", wave);
        gx_draw_text_ui(32.0f, ys, wbuf, FONT_UI_36_BLUE, (0xFFFFFF00) | alpha);

        gx_bind_sprites();
        gx_draw_sprite(32.0f, ys + 48.0f, 276.0f, 2.0f,
                       SPRITE_BAR_BLUE_U0, SPRITE_BAR_BLUE_V0,
                       SPRITE_BAR_BLUE_U1, SPRITE_BAR_BLUE_V1,
                       (0x4DFED200) | alpha);

        char sbuf[32];
        snprintf(sbuf, sizeof(sbuf), "SCORE: %06d", score);
        gx_draw_text_ui(32.0f, (ys * 1.2f) + 10.0f, sbuf, FONT_UI_18_WHITE, (0xFFFFFF00) | alpha);
    }

    // Idle hint
    if (idleTimer > 8.0f) {
        float aa = (idleTimer - 8.0f);
        if (aa > 1.0f) aa = 1.0f;
        float pulse = (sinf(idleTimer * 4.0f) * 0.25f + 0.75f) * aa;
        u8 ha = (u8)(pulse * 255.0f);
        gx_draw_text_ui_center(GAME_W / 2, GAME_H - 180, "Type the words to shoot!\nENTER for EMP",
                               FONT_UI_18_WHITE, (0xFFFFFF00) | ha);
    }
}

static void renderTitle(void) {
    renderBackground();

    float trans = (state == STATE_INTRO_TRANSITION) ? (titleTransitionTime / 2.0f) : 0.0f;
    float titleAlpha = 1.0f - trans;
    if (titleAlpha < 0.0f) titleAlpha = 0.0f;
    u8 ta = (u8)(titleAlpha * 255.0f);

    float phobosY = 60.0f - inOutQuad(trans, 0.0f, 2100.0f, 1.0f);
    float logoY = 112.5f - inOutQuad(trans, 0.0f, 750.0f, 1.0f);
    float newGameY = inOutQuad(trans, 555.0f, 1200.0f, 1.0f);

    // 1. PhobosLab
    gx_bind_sprites();
    gx_set_blend_additive();
    gx_draw_sprite((GAME_W - 154.5f) / 2.0f, phobosY, 154.5f, 18.0f,
                   SPRITE_PHOBOSLAB_U0, SPRITE_PHOBOSLAB_V0,
                   SPRITE_PHOBOSLAB_U1, SPRITE_PHOBOSLAB_V1,
                   (0xFFFFFF00) | ta);

    // 2. ZType logo
    gx_bind_title();
    gx_draw_sprite((GAME_W - 384.0f) / 2.0f, logoY, 384.0f, 172.0f,
                   TITLE_LOGO_U0, TITLE_LOGO_V0,
                   TITLE_LOGO_U1, TITLE_LOGO_V1,
                   (0xFFFFFF00) | ta);

    // 3. Exhaust puffs
    float t_sec = bg_stars_scroll * (60.0f / 10.0f) / 60.0f;
    for (int i = 0; i < 10; i++) {
        float ey = fmodf(i * 139.0f + t_sec * 53.0f * (1.0f + i / 30.0f), 400.0f);
        float a = (ey < 20.0f ? (ey / 20.0f) * 0.7f : (0.7f - ey / 400.0f > 0.0f ? 0.7f - ey / 400.0f : 0.0f)) * 0.3f;
        gx_bind_sprites();
        gx_draw_sprite(227.5f + sinf(t_sec + ey / 30.0f) * 0.7f, player_y + ey, 23.0f, 32.0f,
                       SPRITE_EXHAUST_U0, SPRITE_EXHAUST_V0,
                       SPRITE_EXHAUST_U1, SPRITE_EXHAUST_V1,
                       (0x4DFED200) | (u8)(a * 255.0f));
    }

    // 4. Ship
    gx_bind_title();
    gx_draw_sprite(227.5f, player_y - 7.0f, 23.0f, 297.0f,
                   TITLE_SHIP_U0, TITLE_SHIP_V0,
                   TITLE_SHIP_U1, TITLE_SHIP_V1,
                   0xFFFFFFFF);

    gx_set_blend_alpha();

    // 5. "new game"
    gx_draw_text_ui_center(GAME_W / 2, newGameY, "new game", FONT_UI_36_ORANGE,
                           (0xFFFFFF00) | ta);
}

static void renderGameOver(void) {
    renderBackground();

    float ys = 25.0f;
    float totalKeys = (float)(hits + misses);
    float acc = (totalKeys > 0.0f) ? (hits / totalKeys * 100.0f) : 0.0f;

    // Two-column layout matching Dominic Szablewski's MenuGameOver
    gx_draw_text_ui(24.0f, ys + 0.0f, "FINAL SCORE", FONT_UI_18_WHITE, 0xFFFFFF80);
    gx_draw_text_ui(252.0f, ys + 0.0f, "YOU REACHED", FONT_UI_18_WHITE, 0xFFFFFF80);

    char sbuf[32];
    snprintf(sbuf, sizeof(sbuf), "%06d", score);
    gx_draw_text_ui(24.0f, ys + 25.0f, sbuf, FONT_UI_36_BLUE, 0xFFFFFFFF);

    char wbuf[32];
    snprintf(wbuf, sizeof(wbuf), "WAVE %03d", wave);
    gx_draw_text_ui(252.0f, ys + 25.0f, wbuf, FONT_UI_36_BLUE, 0xFFFFFFFF);

    // Separator bar 1
    gx_bind_sprites();
    gx_draw_sprite(24.0f, ys + 70.0f, 432.0f, 2.0f,
                   SPRITE_BAR_BLUE_U0, SPRITE_BAR_BLUE_V0,
                   SPRITE_BAR_BLUE_U1, SPRITE_BAR_BLUE_V1,
                   0x4DFED2FF);

    gx_draw_text_ui(24.0f, ys + 140.0f, "ACCURACY", FONT_UI_18_WHITE, 0xFFFFFF80);
    gx_draw_text_ui(252.0f, ys + 140.0f, "LONGEST STREAK", FONT_UI_18_WHITE, 0xFFFFFF80);

    char abuf[32];
    snprintf(abuf, sizeof(abuf), "%.1f%%", acc);
    gx_draw_text_ui(24.0f, ys + 165.0f, abuf, FONT_UI_36_BLUE, 0xFFFFFFFF);

    char lbuf[32];
    snprintf(lbuf, sizeof(lbuf), "%d", longestStreak);
    gx_draw_text_ui(252.0f, ys + 165.0f, lbuf, FONT_UI_36_BLUE, 0xFFFFFFFF);

    // Separator bar 2
    gx_bind_sprites();
    gx_draw_sprite(24.0f, ys + 210.0f, 432.0f, 2.0f,
                   SPRITE_BAR_BLUE_U0, SPRITE_BAR_BLUE_V0,
                   SPRITE_BAR_BLUE_U1, SPRITE_BAR_BLUE_V1,
                   0x4DFED2FF);

    // Personal best badge
    if (isPersonalBest) {
        gx_bind_sprites();
        gx_draw_sprite(24.0f, ys + 250.0f, 27.0f, 31.0f,
                       SPRITE_PERSONAL_BEST_U0, SPRITE_PERSONAL_BEST_V0,
                       SPRITE_PERSONAL_BEST_U1, SPRITE_PERSONAL_BEST_V1,
                       0xFFFFFFFF);
        gx_draw_text_ui(60.0f, ys + 255.0f, "NEW PERSONAL BEST", FONT_UI_18_WHITE, 0xFFFFFFFF);
    }

    // "back to title"
    gx_draw_text_ui_center(GAME_W / 2, 590.0f, "back to title", FONT_UI_36_ORANGE, 0xFFFFFFFF);
}

static void renderPause(void) {
    renderBackground();
    renderEnemies();
    renderPlayer();
    renderHUD();

    gx_draw_rect(0, 0, GAME_W, GAME_H, 0x000000B0);
    gx_draw_text_ui_center(GAME_W / 2, 280.0f, "PAUSED", FONT_UI_36_BLUE, 0xFFFFFFFF);
    gx_draw_text_ui_center(GAME_W / 2, 340.0f, "PRESS ENTER OR ESC TO RESUME", FONT_UI_18_WHITE, 0x4DFED2FF);
}

static void render(void) {
    gx_begin_frame();

    // 1. Draw web page background and drop shadows in 640x480 screen viewport
    gx_draw_page_backdrop();

    // 2. Set 480x720 centered canvas viewport and draw game
    gx_set_canvas_viewport();

    if (state == STATE_TITLE || state == STATE_INTRO_TRANSITION) {
        renderTitle();
    } else if (state == STATE_PLAY) {
        renderBackground();
        renderEnemies();
        renderPlayer();
        renderHUD();
    } else if (state == STATE_PAUSE) {
        renderPause();
    } else if (state == STATE_OVER) {
        renderGameOver();
    }

    gx_end_frame();
}

static void startPressed(void) {
    if (state == STATE_TITLE) {
        setGame();
    } else if (state == STATE_OVER) {
        state = STATE_TITLE;
        player_y = 400.0f;
    } else if (state == STATE_PAUSE) {
        state = STATE_PLAY;
    }
}

/* ---------------- Main ---------------- */
int main(int argc, char **argv) {
    WPAD_Init();
    KEYBOARD_Init(NULL);
    sound_init();
    sdOK = fatInitDefault();
    hsLoad();
    gx_init();

    rng_seed = SYS_Time();

    for (;;) {
        WPAD_ScanPads();
        u32 d = WPAD_ButtonsDown(0);
        if (d & WPAD_BUTTON_HOME) break;
        if (d & WPAD_BUTTON_A) startPressed();
        if (d & WPAD_BUTTON_B) { if (state == STATE_PLAY) triggerEMP(); }
        if (d & WPAD_BUTTON_MINUS) sound_toggle_mute();
        if (d & WPAD_BUTTON_PLUS) {
            if (state == STATE_PLAY) state = STATE_PAUSE;
            else if (state == STATE_PAUSE) state = STATE_PLAY;
        }

        keyboard_event ke;
        while (KEYBOARD_GetEvent(&ke)) {
            if (ke.type == KEYBOARD_PRESSED) {
                if (ke.symbol == KS_Return) {
                    if (state == STATE_TITLE || state == STATE_OVER) startPressed();
                    else if (state == STATE_PAUSE) state = STATE_PLAY;
                    else if (state == STATE_PLAY) triggerEMP();
                } else if (ke.symbol == KS_space) {
                    if (state == STATE_PLAY) triggerEMP();
                    else startPressed();
                } else if (ke.symbol == KS_Escape) {
                    if (state == STATE_PLAY) state = STATE_PAUSE;
                    else if (state == STATE_PAUSE) state = STATE_PLAY;
                } else if (ke.symbol == KS_Tab) {
                    sound_toggle_mute();
                } else if (state == STATE_PLAY && ke.symbol < 128) {
                    handleKey((char)ke.symbol);
                }
            }
        }

        update();
        render();
    }

    KEYBOARD_Deinit();
    return 0;
}
