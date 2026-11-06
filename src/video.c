#include "video.h"
#include "sintab.h"
#include <string.h>

/* First pattern of each WDFONTH bank.  The original computes these as
 * char + 0x29 / 0x69 / 0xa9 / 0xe9, which is the same thing given that the
 * banks start at pattern 73 and a glyph's index is (char - ' '). */
static const int font_base[5] = { 73, 73, 137, 201, 265 };

void vid_init(Video *v, const Dar *dar)
{
    memset(v, 0, sizeof *v);
    v->dar = dar;
    v->full = 1;
    v->raster = 1;
}

void vid_tag(Video *v, int id, int fx, int fy)
{
    v->tag_id = id;
    v->tag_fx = fx;
    v->tag_fy = fy;
}

static void record(Video *v, int x, int y, int pat, int flags)
{
    VidCmd *c;

    if (v->ncmd < MAX_CMD) {
        c = &v->cmd[v->ncmd++];
        c->pat = pat;
        c->x = (x + v->ox) * 256 + v->tag_fx;
        c->y = (y + v->oy) * 256 + v->tag_fy;
        c->id = v->tag_id;
        c->flags = flags;
    }
    vid_tag(v, 0, 0, 0);
}

void vid_clear(Video *v, unsigned char index)
{
    memset(v->px, index, sizeof v->px);
}

void vid_dirty(Video *v, int x, int y, int w, int h)
{
    Rect *r;

    if (v->ndirty >= MAX_DIRTY)
        return;
    r = &v->dirty[v->ndirty++];
    r->x = x + v->ox;
    r->y = y + v->ox;   /* yes, ox - see the header */
    r->w = w;
    r->h = h;
}

void vid_dirty_all(Video *v)
{
    v->ndirty = 0;
    v->full = 1;
}

void vid_pat(Video *v, int x, int y, int pat)
{
    if (v->raster)
        dar_blit(v->dar, pat, v->px, SCR_W, SCR_W, SCR_H, x + v->ox, y + v->oy);
    if (v->record)
        record(v, x, y, pat, 0);
}

/* Recorded as VC_TINT: the only remap the game uses is ship 1's. */
void vid_pat_remap(Video *v, int x, int y, int pat, const unsigned char *lut)
{
    if (v->raster)
        dar_blit_remap(v->dar, pat, v->px, SCR_W, SCR_W, SCR_H,
                       x + v->ox, y + v->oy, lut);
    if (v->record)
        record(v, x, y, pat, VC_TINT);
}

void vid_pat_wave(Video *v, int x, int y, int pat, int wave, int amp, int phase)
{
    int dx[SCR_H];
    const DarPat *p;
    int row, neg = 0, step;

    if (pat < 0 || pat >= v->dar->count)
        return;
    p = &v->dar->pat[pat];
    if (p->h > SCR_H)
        return;
    if (wave < 0) {
        neg = 1;
        wave = -wave;
    }
    if (wave == 0)
        wave = 1;
    /* The original walks 0x200 per row and divides by the wavelength before
     * indexing the table, so the phase advances 0x200/wave entries per row. */
    step = 0x200 / wave;

    for (row = 0; row < p->h; row++) {
        int a = (neg && (row & 1)) ? -amp : amp;

        dx[row] = (wd_sin[(phase + row * step) & 0xff] * a) >> 7;
    }
    if (v->raster)
        dar_blit_rowshift(v->dar, pat, v->px, SCR_W, SCR_W, SCR_H,
                          x + v->ox, y + v->oy, dx);
    if (v->record)
        for (row = 0; row < p->h; row++)
            record(v, x + dx[row], y + row, pat, VC_ROW | (row << 8));
}

static int text(Video *v, int x, int y, const char *s, int bank, int adv)
{
    int n = 0, cx = x, base;

    if (bank < 1 || bank > 4)
        bank = 1;
    base = font_base[bank];
    for (; *s; s++, n++, cx += adv)
        vid_pat(v, cx, y, base + ((unsigned char)*s - ' '));
    vid_dirty(v, x, y, n * adv, 16);
    return n;
}

int vid_text(Video *v, int x, int y, const char *s, int bank)
{
    return text(v, x, y, s, bank, 8);
}

int vid_text_wide(Video *v, int x, int y, const char *s, int bank)
{
    return text(v, x, y, s, bank, 12);
}
