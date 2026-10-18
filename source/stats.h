#ifndef STATS_H
#define STATS_H

/* Everything ZType keeps in localStorage (best score, volumes, game history), saved on the SD card */
typedef struct {
    int score, wave, streak;
    float accuracy;
    long created;
} GameRec;

#define STATS_MAX 100

void stats_init(void);                    /* mounts the SD card and loads the saved data */
int stats_sd_ok(void);

int stats_personal_best(void);
void stats_set_personal_best(int score);

float stats_sound_volume(void);           /* 0..1 */
float stats_music_volume(void);
void stats_set_volumes(float sound, float music);

int stats_game_count(void);
const GameRec *stats_game(int i);         /* oldest first */
void stats_add_game(int score, int wave, int streak, float accuracy);

char *stats_read_text(void);              /* sd:/apps/typeshot/text.txt, malloc'ed, or NULL */

#endif
