#include "smf.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const unsigned char *p, *end;
} Rd;

static unsigned be16(const unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }
static unsigned be32(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8) | p[3];
}

/* MIDI variable-length quantity. */
static unsigned vlq(Rd *r)
{
    unsigned v = 0;
    int n = 0;

    while (r->p < r->end && n < 5) {
        unsigned char b = *r->p++;

        v = (v << 7) | (b & 0x7f);
        n++;
        if (!(b & 0x80))
            break;
    }
    return v;
}

static int cmp_ev(const void *a, const void *b)
{
    const SmfEvent *x = (const SmfEvent *)a, *y = (const SmfEvent *)b;

    if (x->tick != y->tick)
        return x->tick < y->tick ? -1 : 1;
    /* Tempo changes first, so a note starting on the same tick is already
     * running at the new speed. */
    if ((x->status == 0xff) != (y->status == 0xff))
        return x->status == 0xff ? -1 : 1;
    return 0;
}

int smf_open(Smf *s, const unsigned char *data, long size)
{
    unsigned hlen;
    int ntrk, t, cap;
    long o;

    memset(s, 0, sizeof *s);
    if (size < 14 || memcmp(data, "MThd", 4) != 0)
        return -1;
    hlen = be32(data + 4);
    if (hlen < 6 || 8 + (long)hlen > size)
        return -1;
    ntrk = (int)be16(data + 10);
    s->division = (int)be16(data + 12);
    s->usec0 = 500000;
    if (s->division <= 0 || (s->division & 0x8000))
        return -1;            /* SMPTE timing; neither WinDepth file uses it */

    cap = 1024;
    s->ev = (SmfEvent *)malloc((size_t)cap * sizeof *s->ev);
    if (!s->ev)
        return -1;

    o = 8 + (long)hlen;
    for (t = 0; t < ntrk && o + 8 <= size; t++) {
        unsigned tlen = be32(data + o + 4);
        unsigned tick = 0;
        unsigned char run = 0;
        Rd r;

        if (memcmp(data + o, "MTrk", 4) != 0)
            break;
        if (o + 8 + (long)tlen > size)
            tlen = (unsigned)(size - o - 8);
        r.p = data + o + 8;
        r.end = r.p + tlen;

        while (r.p < r.end) {
            unsigned char st;
            SmfEvent e;

            tick += vlq(&r);
            if (r.p >= r.end)
                break;
            st = *r.p;
            if (st & 0x80) {
                run = st;
                r.p++;
            } else {
                st = run;
            }
            if (!st)
                break;

            memset(&e, 0, sizeof e);
            e.tick = tick;

            if (st == 0xff) {               /* meta */
                unsigned char mt;
                unsigned len;
                int tempo = 0;

                if (r.p >= r.end)
                    break;
                mt = *r.p++;
                len = vlq(&r);
                if (r.p + len > r.end)
                    break;
                if (mt == 0x51 && len == 3) {
                    e.status = 0xff;
                    e.usec = ((unsigned)r.p[0] << 16) |
                             ((unsigned)r.p[1] << 8) | r.p[2];
                    tempo = 1;
                }
                /* The payload has to be consumed either way - jumping straight
                 * to `emit` from the tempo case leaves the three tempo bytes in
                 * the stream, and they get read as the next delta time. */
                r.p += len;
                run = 0;                    /* a status byte cancels running status */
                if (tempo)
                    goto emit;
                if (mt == 0x2f)             /* end of track */
                    break;
                continue;
            }
            if (st == 0xf0 || st == 0xf7) { /* sysex */
                unsigned len = vlq(&r);

                if (r.p + len > r.end)
                    break;
                r.p += len;
                run = 0;
                continue;
            }
            {
                int nd = ((st & 0xf0) == 0xc0 || (st & 0xf0) == 0xd0) ? 1 : 2;

                if (r.p + nd > r.end)
                    break;
                e.status = st;
                e.d1 = r.p[0];
                e.d2 = nd > 1 ? r.p[1] : 0;
                r.p += nd;
            }
        emit:
            if (s->nev >= cap) {
                SmfEvent *ne;

                cap *= 2;
                ne = (SmfEvent *)realloc(s->ev, (size_t)cap * sizeof *s->ev);
                if (!ne) {
                    smf_close(s);
                    return -1;
                }
                s->ev = ne;
            }
            s->ev[s->nev++] = e;
            if (tick > s->tick_end)
                s->tick_end = tick;
        }
        o += 8 + (long)tlen;
    }

    if (s->nev == 0) {
        smf_close(s);
        return -1;
    }
    /* Stable enough: qsort is not stable, but only same-tick events of the same
     * kind can be reordered and those are independent. */
    qsort(s->ev, (size_t)s->nev, sizeof *s->ev, cmp_ev);
    return 0;
}

void smf_close(Smf *s)
{
    free(s->ev);
    memset(s, 0, sizeof *s);
}
