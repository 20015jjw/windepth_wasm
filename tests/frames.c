/* Headless harness: run the game for N ticks and dump the surface as PNGs, so
 * the port can be eyeballed without opening a window.
 *
 *   frames <out-prefix> <tick>[,<tick>...] [--start N] [--keys MASK]
 *                       [--stage N] [--seed N] [--players 2] [--live]
 *
 * --trace prints every mode/state transition, which is the quickest way to see
 * the attract -> play -> game over -> splash cycle.
 *
 * --start N presses "Start" at tick N; --keys applies a fixed pad mask from
 * then on; --stage jumps straight to a stage so the challenge and boss rounds
 * can be reached without playing through; --auto drives a crude autopilot that
 * sweeps left and right and drops a charge every few frames, which is enough
 * to exercise kills, chains, items and the plane.  With --players 2 it starts
 * a co-op run and drives the second ship too, mirrored.
 *
 * --live prints the most enemies alive at once over the run, which is how the
 * co-op challenge scripts are checked against the 33 enemy slots.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/dar.h"
#include "../src/video.h"
#include "../src/game.h"

extern void png_indexed(const char *path, int w, int h, const unsigned char *px,
                        const unsigned char pal[256][3]);

void wd_sound(int slot, int loop) { (void)slot; (void)loop; }
void wd_music(int which, int on)  { (void)which; (void)on; }

int main(int argc, char **argv)
{
    Dar dar;
    Video vid;
    Game game;
    int want[64], nwant = 0, maxtick = 0, i, t;
    int startat = -1, stage = 0, seed = 1, autoplay = 0, trace = 0;
    int players = 1, live = 0, maxlive = 0;
    int lastmode = -1, laststate = -1;
    unsigned keys = 0;
    const char *prefix = argc > 1 ? argv[1] : "frame";

    if (argc > 2) {
        char *s = argv[2], *p;

        while ((p = strtok(s, ",")) != NULL && nwant < 64) {
            want[nwant++] = atoi(p);
            s = NULL;
        }
    }
    for (i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--start") && i + 1 < argc)
            startat = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc)
            keys = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--stage") && i + 1 < argc)
            stage = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--auto"))
            autoplay = 1;
        else if (!strcmp(argv[i], "--trace"))
            trace = 1;
        else if (!strcmp(argv[i], "--players") && i + 1 < argc)
            players = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--live"))
            live = 1;
    }
    for (i = 0; i < nwant; i++)
        if (want[i] > maxtick)
            maxtick = want[i];

    if (dar_load(&dar, "data/windepth.dar") != 0) {
        fprintf(stderr, "cannot read data/windepth.dar\n");
        return 1;
    }
    vid_init(&vid, &dar);
    game_init(&game, &vid, &dar, "data/windepth.dat");
    game.rnd = (unsigned)seed;

    for (t = 1; t <= maxtick; t++) {
        if (t == startat) {
            game_start(&game, players);
            if (stage > 0) {
                game_stage(&game, stage);
                printf("stage %d: kind %d norma %d maxnum %d total %d "
                       "thr %d,%d,%d,%d,%d nchal %d\n",
                       stage, game.st.kind, game.st.norma, game.st.maxnum,
                       game.st.total, game.st.thr[0], game.st.thr[1],
                       game.st.thr[2], game.st.thr[3], game.st.thr[4],
                       game.st.nchal);
            }
        }
        if (autoplay && startat >= 0 && t > startat) {
            int phase = (t - startat) % 240;

            game.pad = phase < 120 ? PAD_RIGHT : PAD_LEFT;
            /* three frames on, three off, so the release latch re-arms */
            if ((t / 3) % 2 == 0)
                game.pad |= PAD_BTN1;
            if ((t / 5) % 2 == 0)
                game.pad |= PAD_BTN2;
            if (players == 2) {
                game.pad2 = phase < 120 ? PAD_LEFT : PAD_RIGHT;
                if ((t / 4) % 2 == 0)
                    game.pad2 |= PAD_BTN1;
                if ((t / 3) % 2 == 0)
                    game.pad2 |= PAD_BTN2;
            }
        } else {
            game.pad = (startat >= 0 && t > startat) ? keys : 0;
        }
        game_tick(&game);
        if (game.mode == MODE_PLAY && game.live_enemies > maxlive)
            maxlive = game.live_enemies;
        if (trace && (game.mode != lastmode || game.pstate != laststate)) {
            printf("t=%-5d mode %d -> %d, pstate %d -> %d  stage=%d score=%u\n",
                   t, lastmode, game.mode, laststate, game.pstate,
                   game.stageno, game.score);
            lastmode = game.mode;
            laststate = game.pstate;
        }
        for (i = 0; i < nwant; i++)
            if (want[i] == t) {
                char path[512];

                sprintf(path, "%s%04d.png", prefix, t);
                png_indexed(path, SCR_W, SCR_H, vid.px, dar.pal);
                printf("t=%-5d mode=%d pstate=%d stage=%2d norma=%3d live=%2d "
                       "lives=%d boss=%2d/%2d score=%-8u %s\n", t, game.mode,
                       game.pstate, game.stageno, game.st.norma,
                       game.live_enemies, game.lives, game.boss.state,
                       game.boss.hp, game.score, path);
            }
    }
    if (live)
        printf("most enemies alive at once: %d (of %d slots); challenge "
               "script %d, sunk %d\n", maxlive, MAX_ENEMY, game.chal_total,
               game.chal_kills);
    dar_free(&dar);
    return 0;
}
