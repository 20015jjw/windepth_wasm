/* windepth.dat - the stage table, a plain text file the original parses in
 * FUN_004030b0.  Lines starting with ';', '#' or '/' are comments.
 *
 *   Begin <n>            start of stage n's block
 *     Normal             <norma>, <maxnum>, <item>, <k0>..<k4>
 *     Challenge          one line per scripted enemy:
 *                        <time>, <kind>, <xy>, <vx>, <vy>, <chtime>, <nvx>, <nvy>
 *     Boss               <a>, <b>, <c>
 *   End <n>
 */
#ifndef STAGE_H
#define STAGE_H

#define MAX_CHAL 60

enum { ST_NONE = 0, ST_NORMAL = 1, ST_CHALLENGE = 2, ST_BOSS = 3 };

typedef struct {
    int time, kind, xy, vx, vy, chtime, nvx, nvy;
} ChalEnt;

typedef struct {
    int kind;
    int norma;          /* enemies still to kill */
    int maxnum;         /* concurrent enemy cap */
    int item;
    int kcount[5];      /* relative weight of each enemy kind */
    int total;          /* sum of kcount - the modulus of the weight roll */
    int thr[5];         /* prefix sums; == total for a kind that never spawns */
    int fish_at;        /* norma value at which the item-carrying fish appears */
    int nchal;          /* scripted entries left to spawn */
    ChalEnt chal[MAX_CHAL];
    int boss[3];
} Stage;

/* Load stage `no` from `path`.  Returns 0 on success, -1 if the file cannot be
 * read or the stage has no block (the original treats both as fatal). */
int stage_load(Stage *st, const char *path, int no);

#endif
