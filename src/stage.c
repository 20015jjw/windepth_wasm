#include "stage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t')
        s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t'))
        *--e = 0;
    return s;
}

/* The thresholds the spawn roll is compared against.  The original walks kinds
 * 4..0 and takes the first whose threshold is <= the roll, so a kind with a
 * zero weight is given a threshold of `total`, which no roll can reach. */
static void weights(Stage *st)
{
    int k, acc = 0;

    st->total = 0;
    for (k = 0; k < 5; k++)
        st->total += st->kcount[k];
    for (k = 0; k < 5; k++) {
        st->thr[k] = st->kcount[k] > 0 ? acc : st->total;
        acc += st->kcount[k];
    }
    if (st->total < 1)
        st->total = 1;
}

int stage_load(Stage *st, const char *path, int no)
{
    FILE *f = fopen(path, "r");
    char line[512];
    int state = 0;   /* 0 = looking for Begin, 1 = looking for a kind, 2 = data */

    memset(st, 0, sizeof *st);
    if (!f)
        return -1;

    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);

        if (!*s || *s == ';' || *s == '#' || *s == '/')
            continue;
        if (state == 0 && *s == 'B') {
            int n, r = sscanf(s, "Begin %d", &n);

            /* "Boss" also starts with B, and stage 19's block sits before
             * stage 3's in the file, so a literal mismatch has to be skipped.
             * Only a read failure ends the scan - which is what the original
             * checks for too (it tests sscanf's result against -1). */
            if (r == EOF)
                break;
            if (r == 1 && n == no)
                state = 1;
            continue;
        }
        if (state == 2 && *s == 'E')
            break;
        if (state == 1) {
            if (strncmp(s, "Normal", 6) == 0)      { st->kind = ST_NORMAL;    state = 2; continue; }
            if (strncmp(s, "Challenge", 9) == 0)   { st->kind = ST_CHALLENGE; state = 2; continue; }
            if (strncmp(s, "Boss", 4) == 0)        { st->kind = ST_BOSS;      state = 2; continue; }
        }
        if (state != 2)
            continue;

        if (st->kind == ST_NORMAL) {
            int v[8] = {0};
            sscanf(s, "%d , %d , %d , %d , %d , %d , %d , %d",
                   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
            st->norma  = v[0];
            st->maxnum = v[1];
            st->item   = v[2];
            memcpy(st->kcount, v + 3, sizeof st->kcount);
            weights(st);
            st->fish_at = (st->norma / 3) * 2;
        } else if (st->kind == ST_CHALLENGE) {
            ChalEnt *c;
            if (st->nchal >= MAX_CHAL)
                continue;
            c = &st->chal[st->nchal];
            if (sscanf(s, "%d , %d , %d , %d , %d , %d , %d , %d",
                       &c->time, &c->kind, &c->xy, &c->vx, &c->vy,
                       &c->chtime, &c->nvx, &c->nvy) < 3)
                continue;
            st->nchal++;
        } else if (st->kind == ST_BOSS) {
            sscanf(s, "%d , %d , %d", &st->boss[0], &st->boss[1], &st->boss[2]);
        }
    }
    fclose(f);
    return st->kind == ST_NONE ? -1 : 0;
}
