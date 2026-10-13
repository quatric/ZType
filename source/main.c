/*
 * TypeShot Wii - ZType typing shooter for Nintendo Wii
 * Renders with hardware-accelerated GX at 640x480 60fps.
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

/* ---------------- high score (SD card) ---------------- */
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

/* ---------------- game constants & types ---------------- */
#define MAX_ENEMIES 24
#define MAX_BULLETS 48
#define MAX_SPARKS  256
#define MAX_EXPLOSIONS 16
#define MAX_ENEMY_BULLETS 32

enum {
    ENEMY_MISSLE = 0,
    ENEMY_MINE,
    ENEMY_DESTROYER,
    ENEMY_OPPRESSOR
};

typedef struct {
    int active;
    int type;
    char word[24];
    int len;
    int typed;
    int inflight;
    int dying;
    float x, y;
    float vx, vy;
    float angle;
    float spin;
    float shootTimer;
    int shootCount;
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
    float vx, vy;
} EnemyBullet;

typedef struct {
    int active;
    float x, y;
    float radius;
    float maxRadius;
    float alpha;
} EMPWave;

enum {
    STATE_TITLE,
    STATE_PLAY,
    STATE_PAUSE,
    STATE_OVER
};

static int state = STATE_TITLE;
static int wave = 1;
static int score = 0;
static int streak = 0;
static int bestStreak = 0;
static int multiplier = 1;
static int emps = 3;
static int shield = 100;
static int keysHit = 0;
static int keysMiss = 0;
static int wordsKilled = 0;
static int frameCount = 0;
static int targetIndex = -1;
static float reticleTime = 0.0f;

static float player_x = 320.0f;
static float player_y = 430.0f;
static float player_angle = 0.0f;
static float player_target_angle = 0.0f;
static int player_anim_frame = 0;
static int player_anim_timer = 0;

static float bg_stars_scroll = 0.0f;
static float bg_grid_flash = 0.1f;
static int screen_shake = 0;
static int waveClearTimer = 0;
static int newHighScore = 0;
static int kbConnected = 1;

static Enemy enemies[MAX_ENEMIES];
static Plasma plasmas[MAX_BULLETS];
static Spark sparks[MAX_SPARKS];
static Explosion explosions[MAX_EXPLOSIONS];
static EnemyBullet enemyBullets[MAX_ENEMY_BULLETS];
static EMPWave empWave;

static int waveToSpawn = 0;
static int waveSpawned = 0;
static int waveSpawnWait = 0;

static unsigned rng_seed = 123456789;
static int rnd(int max) {
    if (max <= 0) return 0;
    rng_seed = rng_seed * 1103515245 + 12345;
    return (int)((rng_seed >> 16) % (unsigned)max);
}
static float rnd_range(float min, float max) {
    return min + (rnd(10000) / 10000.0f) * (max - min);
}

/* ---------------- particle & effect helpers ---------------- */
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

static void triggerGridFlash(void) {
    bg_grid_flash = 1.0f;
}

static void triggerEMP(void) {
    if (emps <= 0) return;
    emps--;
    sound_play(SFX_EMP);
    triggerScreenShake(20);

    empWave.active = 1;
    empWave.x = player_x;
    empWave.y = player_y;
    empWave.radius = 10.0f;
    empWave.maxRadius = 500.0f;
    empWave.alpha = 1.0f;

    // Clear all enemy bullets
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemyBullets[i].active) {
            spawnSparks(enemyBullets[i].x, enemyBullets[i].y, 4, 0xFFCC44FF, 2.0f);
            enemyBullets[i].active = 0;
        }
    }

    // Damage / destroy enemies
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && !enemies[i].dying) {
            spawnSparks(enemies[i].x, enemies[i].y, 10, 0x4DFED2FF, 3.0f);
            if (enemies[i].type == ENEMY_MINE || enemies[i].type == ENEMY_MISSLE) {
                enemies[i].dying = 1;
                enemies[i].typed = enemies[i].len;
            } else {
                // Remove up to 3 typed letters
                enemies[i].typed += 3;
                if (enemies[i].typed >= enemies[i].len) {
                    enemies[i].dying = 1;
                    enemies[i].typed = enemies[i].len;
                }
            }
            if (enemies[i].dying) {
                spawnExplosion(enemies[i].x, enemies[i].y, 1.2f);
                score += enemies[i].len * 15 * multiplier;
                wordsKilled++;
                triggerGridFlash();
                enemies[i].active = 0;
                if (targetIndex == i) targetIndex = -1;
            }
        }
    }
}

/* ---------------- wave management ---------------- */
static int isFirstLetterUsed(char c) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && !enemies[i].dying) {
            if (enemies[i].word[0] == c) return 1;
        }
    }
    return 0;
}

static const char *pickWord(int minLen, int maxLen) {
    if (minLen < 2) minLen = 2;
    if (maxLen > 12) maxLen = 12;

    for (int tries = 0; tries < 100; tries++) {
        int l = minLen + rnd(maxLen - minLen + 1);
        int count = WORDS_COUNT_BY_LEN[l];
        if (count > 0) {
            const char *w = WORDS_BY_LEN[l][rnd(count)];
            if (!isFirstLetterUsed(w[0]) || tries > 60) {
                return w;
            }
        }
    }
    return "ztype";
}

static void spawnEnemy(int type) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            Enemy *e = &enemies[i];
            memset(e, 0, sizeof(Enemy));
            e->active = 1;
            e->type = type;

            int minL = 3, maxL = 5;
            float speed = 0.5f;

            if (type == ENEMY_MISSLE) {
                minL = 2; maxL = 4; speed = 0.85f;
            } else if (type == ENEMY_MINE) {
                minL = 3; maxL = 6; speed = 0.55f;
            } else if (type == ENEMY_DESTROYER) {
                minL = 7; maxL = 10; speed = 0.35f;
            } else if (type == ENEMY_OPPRESSOR) {
                minL = 9; maxL = 12; speed = 0.22f;
            }

            // Speed factor increases with wave
            speed *= (1.0f + wave * 0.035f);

            const char *w = pickWord(minL, maxL);
            strncpy(e->word, w, sizeof(e->word) - 1);
            e->len = strlen(e->word);

            int wordPixelW = gx_text_width_enemy(e->word);
            int margin = wordPixelW / 2 + 30;
            e->x = margin + rnd(SCREEN_W - margin * 2);
            e->y = -40.0f - rnd(30);
            e->vy = speed;
            e->vx = rnd_range(-0.1f, 0.1f);
            e->angle = 0.0f;
            e->shootTimer = rnd_range(120.0f, 240.0f);
            e->shootCount = 0;

            waveSpawned++;
            return;
        }
    }
}

static void startWave(int w) {
    wave = w;
    waveSpawned = 0;
    waveToSpawn = 5 + w * 2;
    waveSpawnWait = 40;
    waveClearTimer = 0;
    targetIndex = -1;
    sound_play(SFX_SPAWN);
}

static void newGame(void) {
    memset(enemies, 0, sizeof(enemies));
    memset(plasmas, 0, sizeof(plasmas));
    memset(sparks, 0, sizeof(sparks));
    memset(explosions, 0, sizeof(explosions));
    memset(enemyBullets, 0, sizeof(enemyBullets));
    memset(&empWave, 0, sizeof(empWave));

    score = 0;
    streak = 0;
    bestStreak = 0;
    multiplier = 1;
    emps = 3;
    shield = 100;
    keysHit = 0;
    keysMiss = 0;
    wordsKilled = 0;
    frameCount = 0;
    targetIndex = -1;
    reticleTime = 0.0f;
    newHighScore = 0;

    player_x = 320.0f;
    player_y = 430.0f;
    player_angle = 0.0f;
    player_target_angle = 0.0f;

    startWave(1);
    state = STATE_PLAY;
}

/* ---------------- shooting & input ---------------- */
static void shootPlasma(int enemyIdx) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!plasmas[i].active) {
            plasmas[i].active = 1;
            plasmas[i].x = player_x;
            plasmas[i].y = player_y - 12.0f;
            plasmas[i].target_enemy = enemyIdx;
            enemies[enemyIdx].inflight++;

            float dx = enemies[enemyIdx].x - player_x;
            float dy = enemies[enemyIdx].y - player_y;
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
    if (c >= 'A' && c <= 'Z') c += 32;
    if (c < 'a' || c > 'z') return;

    // If no target is locked, acquire lowest enemy starting with this letter
    if (targetIndex < 0) {
        float lowest_y = -9999.0f;
        int best = -1;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (enemies[i].active && !enemies[i].dying && enemies[i].typed == 0) {
                if (enemies[i].word[0] == c && enemies[i].y > lowest_y && enemies[i].y > -20.0f) {
                    lowest_y = enemies[i].y;
                    best = i;
                }
            }
        }
        if (best >= 0) {
            targetIndex = best;
            reticleTime = 1.0f;
            sound_play(SFX_TARGET);
        }
    }

    // Check hit on active target
    if (targetIndex >= 0 && enemies[targetIndex].active) {
        Enemy *e = &enemies[targetIndex];
        if (e->word[e->typed] == c) {
            e->typed++;
            keysHit++;
            streak++;
            if (streak > bestStreak) bestStreak = streak;

            // Multiplier steps
            int oldMul = multiplier;
            if (streak >= 50) multiplier = 3;
            else if (streak >= 25) multiplier = 2;
            else multiplier = 1;

            if (multiplier > oldMul) {
                if (multiplier == 2) sound_play(SFX_MULTI_2);
                else if (multiplier == 3) sound_play(SFX_MULTI_3);
            }

            score += 10 * multiplier;

            // Orient player towards target
            float dx = e->x - player_x;
            float dy = e->y - player_y;
            player_target_angle = atan2f(dx, -dy);

            shootPlasma(targetIndex);

            if (e->typed >= e->len) {
                e->dying = 1;
                targetIndex = -1;
            }
            return;
        }
    }

    // Miss!
    keysMiss++;
    streak = 0;
    multiplier = 1;
    sound_play(SFX_CLICK);
    player_anim_frame = 4;
    player_anim_timer = 6;
    spawnSparks(player_x, player_y - 10.0f, 6, 0xFF4444FF, 2.0f);
}

/* ---------------- update logic ---------------- */
static void killEnemy(int i) {
    Enemy *e = &enemies[i];
    e->active = 0;
    wordsKilled++;
    score += e->len * 20 * multiplier;
    triggerGridFlash();

    if (e->type == ENEMY_MISSLE) {
        sound_play(SFX_EXPLODE_S);
        spawnExplosion(e->x, e->y, 0.9f);
        spawnSparks(e->x, e->y, 16, 0x55FFAAFF, 3.0f);
        triggerScreenShake(4);
    } else if (e->type == ENEMY_MINE) {
        sound_play(SFX_EXPLODE_M);
        spawnExplosion(e->x, e->y, 1.1f);
        spawnSparks(e->x, e->y, 22, 0xFFCC44FF, 3.5f);
        triggerScreenShake(6);
    } else {
        sound_play(SFX_EXPLODE_L);
        spawnExplosion(e->x, e->y, 1.8f);
        spawnSparks(e->x, e->y, 35, 0xFF8844FF, 4.5f);
        spawnSparks(e->x, e->y, 20, 0xFFFFFFFF, 3.0f);
        triggerScreenShake(12);
    }

    if (targetIndex == i) targetIndex = -1;
}

static void update(void) {
    frameCount++;

    // Background scrolls
    bg_stars_scroll += 0.8f;
    if (bg_stars_scroll >= 480.0f) bg_stars_scroll -= 480.0f;

    // Grid flash decays
    if (bg_grid_flash > 0.1f) {
        bg_grid_flash -= 0.04f;
        if (bg_grid_flash < 0.1f) bg_grid_flash = 0.1f;
    }

    // Screen shake decays
    if (screen_shake > 0) screen_shake--;

    // Reticle animation decays
    if (reticleTime > 0.0f) {
        reticleTime -= 0.035f;
        if (reticleTime < 0.0f) reticleTime = 0.0f;
    }

    // Update sparks
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (sparks[i].active) {
            sparks[i].x += sparks[i].vx;
            sparks[i].y += sparks[i].vy;
            sparks[i].vx *= 0.96f;
            sparks[i].vy *= 0.96f;
            if (--sparks[i].life <= 0) sparks[i].active = 0;
        }
    }

    // Update explosions
    for (int i = 0; i < MAX_EXPLOSIONS; i++) {
        if (explosions[i].active) {
            explosions[i].frame += 0.25f;
            if (explosions[i].frame >= 3.0f) explosions[i].active = 0;
        }
    }

    // Update EMP wave
    if (empWave.active) {
        empWave.radius += 12.0f;
        empWave.alpha = 1.0f - (empWave.radius / empWave.maxRadius);
        if (empWave.radius >= empWave.maxRadius) empWave.active = 0;
    }

    if (state != STATE_PLAY) return;

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

    // Smooth player rotation
    float dAngle = player_target_angle - player_angle;
    player_angle += dAngle * 0.18f;

    // Enemy wave spawning
    if (waveSpawned < waveToSpawn) {
        if (--waveSpawnWait <= 0) {
            int type = ENEMY_MINE;
            int r = rnd(100);
            if (wave >= 7 && (r < 15 || (wave % 7 == 0 && waveSpawned == 0))) {
                type = ENEMY_OPPRESSOR;
            } else if (wave >= 3 && (r < 35 || (wave % 3 == 0 && waveSpawned == 0))) {
                type = ENEMY_DESTROYER;
            } else if (wave >= 2 && r < 65) {
                type = ENEMY_MISSLE;
            } else {
                type = ENEMY_MINE;
            }
            spawnEnemy(type);
            waveSpawnWait = 45 - wave * 2;
            if (waveSpawnWait < 18) waveSpawnWait = 18;
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
            if (e->type == ENEMY_MINE) {
                e->angle += 0.04f;
            }

            // Word label margins
            int hw = gx_text_width_enemy(e->word) / 2 + 10;
            if (e->x < hw) { e->x = hw; e->vx = -e->vx; }
            if (e->x > SCREEN_W - hw) { e->x = SCREEN_W - hw; e->vx = -e->vx; }

            // Firing behavior for Destroyer / Oppressor
            if (!e->dying && e->y > 30.0f) {
                if (--e->shootTimer <= 0) {
                    if (e->type == ENEMY_DESTROYER) {
                        // Spawn missile
                        spawnEnemy(ENEMY_MISSLE);
                        e->shootTimer = rnd_range(160.0f, 260.0f);
                    } else if (e->type == ENEMY_OPPRESSOR) {
                        // Fire bullets towards player
                        for (int b = 0; b < MAX_ENEMY_BULLETS; b++) {
                            if (!enemyBullets[b].active) {
                                enemyBullets[b].active = 1;
                                enemyBullets[b].x = e->x;
                                enemyBullets[b].y = e->y + 35.0f;
                                float dx = player_x - e->x + rnd_range(-40.0f, 40.0f);
                                float dy = player_y - (e->y + 35.0f);
                                float d = sqrtf(dx * dx + dy * dy);
                                enemyBullets[b].vx = (dx / d) * 2.2f;
                                enemyBullets[b].vy = (dy / d) * 2.2f;
                                break;
                            }
                        }
                        e->shootTimer = rnd_range(90.0f, 150.0f);
                    }
                }
            }

            // Enemy reaches bottom
            if (e->y > 440.0f) {
                shield -= 25;
                streak = 0;
                multiplier = 1;
                triggerScreenShake(18);
                sound_play(SFX_EXPLODE_L);
                spawnExplosion(e->x, e->y, 1.5f);
                spawnSparks(e->x, e->y, 30, 0xFF4422FF, 4.0f);
                e->active = 0;
                if (targetIndex == i) targetIndex = -1;

                if (shield <= 0) {
                    shield = 0;
                    state = STATE_OVER;
                    sound_play(SFX_EXPLODE_P);
                    spawnExplosion(player_x, player_y, 2.5f);
                    spawnSparks(player_x, player_y, 60, 0x4DFED2FF, 6.0f);
                    triggerScreenShake(30);

                    if (score > hiscore) {
                        hiscore = score;
                        newHighScore = 1;
                        hsSave();
                    }
                }
            }
        }
    }

    // Update enemy bullets
    for (int b = 0; b < MAX_ENEMY_BULLETS; b++) {
        if (enemyBullets[b].active) {
            enemyBullets[b].x += enemyBullets[b].vx;
            enemyBullets[b].y += enemyBullets[b].vy;

            // Hit player
            float dx = enemyBullets[b].x - player_x;
            float dy = enemyBullets[b].y - player_y;
            if (sqrtf(dx * dx + dy * dy) < 18.0f) {
                enemyBullets[b].active = 0;
                shield -= 10;
                streak = 0;
                multiplier = 1;
                triggerScreenShake(8);
                sound_play(SFX_HIT);
                spawnSparks(player_x, player_y, 12, 0xFF6644FF, 3.0f);
                if (shield <= 0) {
                    shield = 0;
                    state = STATE_OVER;
                    sound_play(SFX_EXPLODE_P);
                    spawnExplosion(player_x, player_y, 2.5f);
                    triggerScreenShake(30);
                    if (score > hiscore) { hiscore = score; newHighScore = 1; hsSave(); }
                }
            }

            if (enemyBullets[b].y > 490.0f || enemyBullets[b].x < -20.0f || enemyBullets[b].x > 660.0f) {
                enemyBullets[b].active = 0;
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
                p->x += (dx / d) * 14.0f;
                p->y += (dy / d) * 14.0f;
            }
        }
    }

    // Wave clear check
    if (waveSpawned >= waveToSpawn && aliveCount == 0) {
        if (waveClearTimer == 0) {
            waveClearTimer = 120; // 2 seconds banner
            score += 150 * wave;
            if (emps < 3) emps++;
            if (shield < 100) { shield += 20; if (shield > 100) shield = 100; }
        } else {
            if (--waveClearTimer == 0) {
                startWave(wave + 1);
            }
        }
    }
}

/* ---------------- render functions ---------------- */
static void drawBackground(void) {
    // 1. Scrolling starfield
    gx_bind_stars();
    gx_draw_sprite(0, bg_stars_scroll - 480.0f, SCREEN_W, 480, 0, 0, 1, 1, 0xFFFFFFFF);
    gx_draw_sprite(0, bg_stars_scroll, SCREEN_W, 480, 0, 0, 1, 1, 0xFFFFFFFF);

    // 2. Space gradient overlay (alpha 0.8)
    gx_draw_gradient(0, 0, SCREEN_W, SCREEN_H, 0x031214D0, 0x234442D0);

    // 3. Repeating cyber grid with flash
    gx_bind_grid();
    gx_set_blend_additive();
    u8 grid_alpha = (u8)(bg_grid_flash * 255.0f);
    u32 grid_col = (0xFFFFFF00) | grid_alpha;
    gx_draw_sprite(0, 0, SCREEN_W, SCREEN_H, 0, 0, 1.2f, 1.0f, grid_col);
    gx_set_blend_alpha();
}

static void drawPlayer(void) {
    gx_set_blend_additive();

    // 1. Exhaust trail puffs
    for (int i = 0; i < 8; i++) {
        float ey = (i * 18 + frameCount * 3) % 80;
        float ex = player_x - 1.0f + sinf(frameCount * 0.2f + i) * 2.0f;
        float alpha = (ey < 10) ? (ey / 10.0f) : (1.0f - (ey / 80.0f));
        u8 a = (u8)(alpha * 160.0f);
        gx_draw_sprite(ex - 6.0f, player_y + 10.0f + ey, 12, 16,
                       SPRITE_EXHAUST_U0, SPRITE_EXHAUST_V0,
                       SPRITE_EXHAUST_U1, SPRITE_EXHAUST_V1,
                       (0x4DFED200) | a);
    }

    // 2. Ship frame (24x24 in sprite sheet at Y=SPRITE_SHIP_Y)
    float u0 = (SPRITE_SHIP_X + player_anim_frame * 24.0f) / 512.0f;
    float v0 = SPRITE_SHIP_V0;
    float u1 = (SPRITE_SHIP_X + (player_anim_frame + 1) * 24.0f) / 512.0f;
    float v1 = SPRITE_SHIP_V1;

    gx_draw_sprite_rot(player_x, player_y, 36, 36, player_angle,
                       u0, v0, u1, v1, 0xFFFFFFFF);

    gx_set_blend_alpha();
}

static void drawTargetReticle(void) {
    if (targetIndex < 0 || !enemies[targetIndex].active) return;
    Enemy *e = &enemies[targetIndex];

    gx_set_blend_additive();

    // Subtle cyan laser beam from player to target
    gx_draw_line(player_x, player_y - 12.0f, e->x, e->y, 1.5f, 0x4DFED244);

    // Zooming reticle
    float scale = (reticleTime > 0.0f) ? (0.45f + reticleTime * reticleTime * 2.0f) : 0.45f;
    float angle = (reticleTime > 0.0f) ? (reticleTime * 3.14159f) : (frameCount * 0.02f);
    float size = 256.0f * scale;

    u8 a = (reticleTime > 0.0f) ? (u8)(sqrtf(1.0f - reticleTime) * 255.0f) : 180;
    gx_draw_sprite_rot(e->x, e->y, size, size, angle,
                       SPRITE_RETICLE_U0, SPRITE_RETICLE_V0,
                       SPRITE_RETICLE_U1, SPRITE_RETICLE_V1,
                       (0x4DFED200) | a);

    gx_set_blend_alpha();
}

static void drawEnemies(void) {
    gx_bind_sprites();
    gx_set_blend_additive();

    // 1. Draw enemy ships
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            Enemy *e = &enemies[i];
            if (e->type == ENEMY_MISSLE) {
                gx_draw_sprite_rot(e->x, e->y, 24, 30, 3.14159f,
                                   SPRITE_MISSLE_U0, SPRITE_MISSLE_V0,
                                   SPRITE_MISSLE_U1, SPRITE_MISSLE_V1,
                                   0xFFFFFFFF);
            } else if (e->type == ENEMY_MINE) {
                gx_draw_sprite_rot(e->x, e->y, 34, 34, e->angle,
                                   SPRITE_MINE_U0, SPRITE_MINE_V0,
                                   SPRITE_MINE_U1, SPRITE_MINE_V1,
                                   0xFFFFFFFF);
            } else if (e->type == ENEMY_DESTROYER) {
                gx_draw_sprite(e->x - 24, e->y - 30, 48, 60,
                               SPRITE_DESTROYER_U0, SPRITE_DESTROYER_V0,
                               SPRITE_DESTROYER_U1, SPRITE_DESTROYER_V1,
                               0xFFFFFFFF);
            } else if (e->type == ENEMY_OPPRESSOR) {
                gx_draw_sprite(e->x - 36, e->y - 45, 72, 90,
                               SPRITE_OPPRESSOR_U0, SPRITE_OPPRESSOR_V0,
                               SPRITE_OPPRESSOR_U1, SPRITE_OPPRESSOR_V1,
                               0xFFFFFFFF);
            }
        }
    }

    // 2. Draw enemy bullets
    for (int b = 0; b < MAX_ENEMY_BULLETS; b++) {
        if (enemyBullets[b].active) {
            gx_draw_sprite(enemyBullets[b].x - 10, enemyBullets[b].y - 12, 20, 24,
                           SPRITE_BULLET_U0, SPRITE_BULLET_V0,
                           SPRITE_BULLET_U1, SPRITE_BULLET_V1,
                           0xFFAA33FF);
        }
    }

    // 3. Draw plasma bolts
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (plasmas[i].active) {
            gx_draw_sprite_rot(plasmas[i].x, plasmas[i].y, 32, 32, plasmas[i].angle,
                               SPRITE_PLASMA_U0, SPRITE_PLASMA_V0,
                               SPRITE_PLASMA_U1, SPRITE_PLASMA_V1,
                               0xFFFFFFFF);
        }
    }

    // 4. Draw explosions
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

    // 5. Draw sparks
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

    // 6. Draw EMP shockwave
    if (empWave.active) {
        u8 a = (u8)(empWave.alpha * 255.0f);
        float sz = empWave.radius * 2.0f;
        gx_draw_sprite(empWave.x - empWave.radius, empWave.y - empWave.radius, sz, sz,
                       SPRITE_EMP_U0, SPRITE_EMP_V0,
                       SPRITE_EMP_U1, SPRITE_EMP_V1,
                       (0x4DFED200) | a);
    }

    gx_set_blend_alpha();

    // 7. Draw enemy word labels (normal alpha blend)
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && !enemies[i].dying) {
            Enemy *e = &enemies[i];
            int textW = gx_text_width_enemy(e->word);
            int boxX = (int)(e->x - textW * 0.5f);
            int boxY = (int)(e->y + 22.0f);

            if (boxX < 10) boxX = 10;
            if (boxX + textW + 8 > SCREEN_W - 10) boxX = SCREEN_W - 10 - textW - 8;

            // Semi-transparent dark background card
            gx_draw_rect(boxX - 4, boxY - 2, textW + 8, 20, 0x000000C0);

            // Active / targeted border highlight
            if (i == targetIndex) {
                gx_draw_rect(boxX - 4, boxY + 18, textW + 8, 2, 0x4DFED2FF);
            }

            // Word with typed letters in orange and untyped letters in white
            gx_draw_text_enemy(boxX, boxY, e->word, e->typed);
        }
    }
}

static void drawHUD(void) {
    char buf[64];

    // Score & Wave (top-left)
    snprintf(buf, sizeof(buf), "SCORE %06d", score);
    gx_draw_text_ui(24, 16, buf, FONT_UI_18_WHITE, 0xFFFFFFFF);

    snprintf(buf, sizeof(buf), "WAVE %03d", wave);
    gx_draw_text_ui(24, 38, buf, FONT_UI_18_WHITE, 0x4DFED2FF);

    // Multiplier progress bar (top-center)
    gx_bind_sprites();
    if (streak > 0) {
        float barLen = (1.0f - 75.0f / (75.0f + streak * 2.0f)) * 220.0f;
        float barX = (SCREEN_W - 220.0f) * 0.5f;
        gx_draw_sprite(barX, 24, barLen, 3,
                       SPRITE_BAR_BLUE_U0, SPRITE_BAR_BLUE_V0,
                       SPRITE_BAR_BLUE_U1, SPRITE_BAR_BLUE_V1,
                       0x4DFED2FF);

        // Multiplier indicator (x2 or x3 badge)
        if (multiplier >= 2) {
            int tile = (multiplier == 3) ? 1 : 0;
            float mu0 = (SPRITE_MULTI_INDICATOR_X + tile * 32.0f) / 512.0f;
            float mv0 = SPRITE_MULTI_INDICATOR_V0;
            float mu1 = (SPRITE_MULTI_INDICATOR_X + (tile + 1) * 32.0f) / 512.0f;
            float mv1 = SPRITE_MULTI_INDICATOR_V1;

            gx_draw_sprite(barX + barLen - 16, 6, 32, 18,
                           mu0, mv0, mu1, mv1, 0xFFFFFFFF);
        }
    }

    // EMP button icons (top-right)
    int empTile = emps;
    if (empTile < 0) empTile = 0;
    if (empTile > 3) empTile = 3;
    float eu0 = (SPRITE_EMP_BUTTONS_X + empTile * 34.0f) / 512.0f;
    float ev0 = SPRITE_EMP_BUTTONS_V0;
    float eu1 = (SPRITE_EMP_BUTTONS_X + (empTile + 1) * 34.0f) / 512.0f;
    float ev1 = SPRITE_EMP_BUTTONS_V1;
    gx_draw_sprite(SCREEN_W - 56, 14, 34, 40, eu0, ev0, eu1, ev1,
                   (emps > 0) ? 0xFFFFFFFF : 0x888888AA);

    // Shield integrity bar (bottom-left)
    gx_draw_rect(24, SCREEN_H - 18, 120, 6, 0x112233C0);
    u32 shieldCol = (shield > 50) ? 0x4DFED2FF : ((shield > 25) ? 0xFFAA33FF : 0xFF3333FF);
    gx_draw_rect(24, SCREEN_H - 18, (120.0f * shield) / 100.0f, 6, shieldCol);
    gx_draw_text_ui(152, SCREEN_H - 22, "SHIELD", FONT_UI_18_WHITE, 0x8899AAFF);

    // Wave Clear Banner
    if (waveClearTimer > 0) {
        float t = waveClearTimer / 120.0f;
        float bannerY = 180.0f + sinf(t * 3.14159f) * 10.0f;
        snprintf(buf, sizeof(buf), "WAVE %03d CLEAR", wave);
        gx_draw_text_ui_center(SCREEN_W / 2, bannerY, buf, FONT_UI_36_BLUE, 0xFFFFFFFF);

        gx_bind_sprites();
        gx_draw_sprite((SCREEN_W - 320) / 2, bannerY + 44, 320, 3,
                       SPRITE_BAR_BLUE_U0, SPRITE_BAR_BLUE_V0,
                       SPRITE_BAR_BLUE_U1, SPRITE_BAR_BLUE_V1,
                       0x4DFED2FF);

        snprintf(buf, sizeof(buf), "SCORE: %06d", score);
        gx_draw_text_ui_center(SCREEN_W / 2, bannerY + 54, buf, FONT_UI_18_WHITE, 0xFFFFFFFF);
    }
}

static void drawTitleScreen(void) {
    drawBackground();

    gx_bind_sprites();
    gx_set_blend_additive();

    // 1. PhobosLab byline banner
    gx_draw_sprite((SCREEN_W - SPRITE_PHOBOSLAB_W) / 2, 45,
                   SPRITE_PHOBOSLAB_W, SPRITE_PHOBOSLAB_H,
                   SPRITE_PHOBOSLAB_U0, SPRITE_PHOBOSLAB_V0,
                   SPRITE_PHOBOSLAB_U1, SPRITE_PHOBOSLAB_V1,
                   0xFFFFFFFF);

    // 2. Large ZType Logo
    gx_bind_title();
    gx_draw_sprite((SCREEN_W - 380) / 2, 85, 380, 170,
                   TITLE_LOGO_U0, TITLE_LOGO_V0,
                   TITLE_LOGO_U1, TITLE_LOGO_V1,
                   0xFFFFFFFF);

    // 3. Hovering Player Ship with thruster
    float shipY = 275.0f + sinf(frameCount * 0.06f) * 5.0f;
    for (int i = 0; i < 6; i++) {
        float ey = (i * 12 + frameCount * 2) % 50;
        float alpha = 1.0f - (ey / 50.0f);
        gx_bind_sprites();
        gx_draw_sprite(SCREEN_W / 2 - 8, shipY + 28 + ey, 16, 20,
                       SPRITE_EXHAUST_U0, SPRITE_EXHAUST_V0,
                       SPRITE_EXHAUST_U1, SPRITE_EXHAUST_V1,
                       (0x4DFED200) | (u8)(alpha * 180));
    }

    gx_bind_title();
    gx_draw_sprite(SCREEN_W / 2 - 16, shipY - 10, 32, 42,
                   TITLE_SHIP_U0, TITLE_SHIP_V0,
                   TITLE_SHIP_U1, TITLE_SHIP_V1,
                   0xFFFFFFFF);

    gx_set_blend_alpha();

    // 4. Menu & prompt text
    float pulse = (sinf(frameCount * 0.1f) * 0.3f + 0.7f);
    u8 pulseAlpha = (u8)(pulse * 255.0f);
    gx_draw_text_ui_center(SCREEN_W / 2, 345, "NEW GAME", FONT_UI_36_WHITE, 0xFFFFFFFF);
    gx_draw_text_ui_center(SCREEN_W / 2, 395, "PRESS ENTER TO PLAY", FONT_UI_18_WHITE,
                           (0x4DFED200) | pulseAlpha);

    gx_draw_text_ui_center(SCREEN_W / 2, 422, "SPACE FOR EMP   -   TAB TO MUTE",
                           FONT_UI_18_WHITE, 0x8899AACC);

    if (hiscore > 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "HIGH SCORE: %06d", hiscore);
        gx_draw_text_ui_center(SCREEN_W / 2, 445, buf, FONT_UI_18_WHITE, 0xFFAA33FF);
    }
}

static void drawGameOver(void) {
    drawBackground();

    // "GAME OVER" title
    gx_draw_text_ui_center(SCREEN_W / 2, 60, "GAME OVER", FONT_UI_36_BLUE, 0xFFFFFFFF);

    // Blue divider bar
    gx_bind_sprites();
    gx_draw_sprite((SCREEN_W - 360) / 2, 105, 360, 3,
                   SPRITE_BAR_BLUE_U0, SPRITE_BAR_BLUE_V0,
                   SPRITE_BAR_BLUE_U1, SPRITE_BAR_BLUE_V1,
                   0x4DFED2FF);

    char buf[64];
    int totalKeys = keysHit + keysMiss;
    int acc = totalKeys ? (keysHit * 100 / totalKeys) : 0;
    int wpm = (frameCount > 0) ? (int)(keysHit / 5.0f / (frameCount / 3600.0f)) : 0;

    snprintf(buf, sizeof(buf), "SCORE: %06d", score);
    gx_draw_text_ui_center(SCREEN_W / 2, 130, buf, FONT_UI_18_WHITE, 0xFFFFFFFF);

    snprintf(buf, sizeof(buf), "WAVE REACHED: %d", wave);
    gx_draw_text_ui_center(SCREEN_W / 2, 160, buf, FONT_UI_18_WHITE, 0x4DFED2FF);

    snprintf(buf, sizeof(buf), "WORDS DESTROYED: %d", wordsKilled);
    gx_draw_text_ui_center(SCREEN_W / 2, 190, buf, FONT_UI_18_WHITE, 0xFFFFFFFF);

    snprintf(buf, sizeof(buf), "ACCURACY: %d%%", acc);
    gx_draw_text_ui_center(SCREEN_W / 2, 220, buf, FONT_UI_18_WHITE, 0xFFFFFFFF);

    snprintf(buf, sizeof(buf), "TYPING SPEED: %d WPM", wpm);
    gx_draw_text_ui_center(SCREEN_W / 2, 250, buf, FONT_UI_18_WHITE, 0xFFFFFFFF);

    snprintf(buf, sizeof(buf), "LONGEST STREAK: %d", bestStreak);
    gx_draw_text_ui_center(SCREEN_W / 2, 280, buf, FONT_UI_18_WHITE, 0xFFFFFFFF);

    snprintf(buf, sizeof(buf), "ALL-TIME BEST: %06d", hiscore);
    gx_draw_text_ui_center(SCREEN_W / 2, 315, buf, FONT_UI_18_WHITE, 0xFFAA33FF);

    if (newHighScore) {
        gx_draw_text_ui_center(SCREEN_W / 2, 345, "NEW PERSONAL BEST!", FONT_UI_18_WHITE, 0x4DFED2FF);
    }

    float pulse = (sinf(frameCount * 0.1f) * 0.3f + 0.7f);
    gx_draw_text_ui_center(SCREEN_W / 2, 395, "PRESS ENTER TO RETRY", FONT_UI_18_WHITE,
                           (0xFFFFFF00) | (u8)(pulse * 255));
}

static void drawPauseScreen(void) {
    drawBackground();
    drawEnemies();
    drawTargetReticle();
    drawPlayer();
    drawHUD();

    // Dark tint
    gx_draw_rect(0, 0, SCREEN_W, SCREEN_H, 0x000000AA);

    gx_draw_text_ui_center(SCREEN_W / 2, 180, "PAUSED", FONT_UI_36_BLUE, 0xFFFFFFFF);
    gx_draw_text_ui_center(SCREEN_W / 2, 240, "PRESS ENTER OR ESC TO RESUME",
                           FONT_UI_18_WHITE, 0x4DFED2FF);
}

static void render(void) {
    gx_begin_frame();

    if (state == STATE_TITLE) {
        drawTitleScreen();
    } else if (state == STATE_PLAY) {
        drawBackground();
        drawEnemies();
        drawTargetReticle();
        drawPlayer();
        drawHUD();
    } else if (state == STATE_PAUSE) {
        drawPauseScreen();
    } else if (state == STATE_OVER) {
        drawGameOver();
    }

    if (!kbConnected && state != STATE_PLAY) {
        gx_draw_text_ui_center(SCREEN_W / 2, SCREEN_H - 24, "KEYBOARD NOT DETECTED",
                               FONT_UI_18_WHITE, 0xFF5555FF);
    }

    gx_end_frame();
}

static void startPressed(void) {
    if (state == STATE_TITLE || state == STATE_OVER) newGame();
    else if (state == STATE_PAUSE) state = STATE_PLAY;
}

/* ---------------- main ---------------- */
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
            if (ke.type == KEYBOARD_CONNECTED) kbConnected = 1;
            else if (ke.type == KEYBOARD_DISCONNECTED) kbConnected = 0;
            else if (ke.type == KEYBOARD_PRESSED) {
                kbConnected = 1;
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
