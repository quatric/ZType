#include "stats.h"
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define SAVE_DIR "sd:/apps/typeshot"
#define STATS_FILE SAVE_DIR "/stats.dat"
#define OLD_SCORE_FILE SAVE_DIR "/highscore.dat"
#define TEXT_FILE SAVE_DIR "/text.txt"

static int sdOK;
static int best;
static float sound_vol = 0.5f, music_vol = 0.5f;   /* the defaults of ZType */
static GameRec games[STATS_MAX];
static int ngames;

static unsigned old_sum(unsigned v) { return (v * 2654435761u) ^ 0x5459u; }

static void save_all(void) {
#ifdef ZT_TEST_SCRIPT
    return;                      /* scripted test runs must not touch the saved data */
#endif
    if (!sdOK) return;
    mkdir("sd:/apps", 0777);
    mkdir(SAVE_DIR, 0777);
    FILE *f = fopen(STATS_FILE, "w");
    if (!f) return;
    fprintf(f, "ZTYPE 1\nbest %d\nvolume %.2f %.2f\n", best, sound_vol, music_vol);
    for (int i = 0; i < ngames; i++)
        fprintf(f, "game %ld %d %d %d %.3f\n", games[i].created, games[i].score, games[i].wave,
                games[i].streak, games[i].accuracy);
    fclose(f);
}

void stats_init(void) {
    sdOK = fatInitDefault();
#ifdef ZT_TEST_SCRIPT
    return;
#endif
    if (!sdOK) return;
    FILE *f = fopen(STATS_FILE, "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            int a, b, c;
            long t;
            float x, y;
            if (sscanf(line, "best %d", &a) == 1) best = a;
            else if (sscanf(line, "volume %f %f", &x, &y) == 2) { sound_vol = x; music_vol = y; }
            else if (sscanf(line, "game %ld %d %d %d %f", &t, &a, &b, &c, &x) == 5 && ngames < STATS_MAX)
                games[ngames++] = (GameRec){a, b, c, x, t};
        }
        fclose(f);
    } else {
        /* best score of the previous version of the port */
        f = fopen(OLD_SCORE_FILE, "r");
        if (f) {
            unsigned v, c;
            if (fscanf(f, "TSHOT1 %u %u", &v, &c) == 2 && c == old_sum(v)) best = (int)v;
            fclose(f);
        }
    }
}

int stats_sd_ok(void) { return sdOK; }

int stats_personal_best(void) { return best; }

void stats_set_personal_best(int score) {
    best = score;
    save_all();
}

float stats_sound_volume(void) { return sound_vol; }
float stats_music_volume(void) { return music_vol; }

void stats_set_volumes(float sound, float music) {
    sound_vol = sound;
    music_vol = music;
    save_all();
}

int stats_game_count(void) { return ngames; }
const GameRec *stats_game(int i) { return &games[i]; }

void stats_add_game(int score, int wave, int streak, float accuracy) {
    if (ngames == STATS_MAX) {
        memmove(games, games + 1, (STATS_MAX - 1) * sizeof(games[0]));
        ngames--;
    }
    games[ngames++] = (GameRec){score, wave, streak, accuracy, (long)time(NULL)};
    save_all();
}

char *stats_read_text(void) {
    if (!sdOK) return NULL;
    FILE *f = fopen(TEXT_FILE, "r");
    if (!f) return NULL;
    char *buf = malloc(64 * 1024 + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, 64 * 1024, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}
