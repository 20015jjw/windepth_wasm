#include "atlas.h"
#include <stdlib.h>
#include <string.h>

#define PAD 2              /* transparent pixels around each pattern, at 1x */

typedef struct { int w, h; unsigned char *px; } Img;   /* premultiplied RGBA */

/* One pattern through the palette, optionally remapped first. */
static int expand(Img *im, const Dar *d, int id, const unsigned char *lut)
{
    const DarPat *p = &d->pat[id];
    unsigned char row[1024];
    int x, y;

    im->w = p->w;
    im->h = p->h;
    im->px = (unsigned char *)calloc((size_t)p->w * p->h, 4);
    if (!im->px)
        return -1;
    for (y = 0; y < p->h; y++) {
        dar_row(p, y, row);
        for (x = 0; x < p->w; x++) {
            unsigned char *o = im->px + ((size_t)y * p->w + x) * 4;
            int c = row[x];

            if (c == p->trans)
                continue;
            if (lut)
                c = lut[c];
            o[0] = d->pal[c][0];
            o[1] = d->pal[c][1];
            o[2] = d->pal[c][2];
            o[3] = 255;
        }
    }
    return 0;
}

/* ------------------------------------------------------------ the filter */

/* Perceptual distance, xBR's YUV weighting, with alpha weighed heavily so a
 * sprite's outline counts as an edge against the transparency around it. */
static int dist(const unsigned char *a, const unsigned char *b)
{
    int dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
    int y = abs(dr * 299 + dg * 587 + db * 114) / 1000;
    int u = abs(-dr * 169 - dg * 331 + db * 500) / 1000;
    int v = abs(dr * 500 - dg * 419 - db * 81) / 1000;

    return 48 * y + 7 * u + 6 * v + 64 * abs(a[3] - b[3]);
}

static const unsigned char *at(const Img *im, int x, int y)
{
    x = x < 0 ? 0 : (x >= im->w ? im->w - 1 : x);
    y = y < 0 ? 0 : (y >= im->h ? im->h - 1 : y);
    return im->px + ((size_t)y * im->w + x) * 4;
}

/* 2x: each pixel E becomes four, and each of those corners is pulled halfway
 * toward its neighbour when the pixels around it say an edge runs across that
 * corner diagonally.  Seen from the bottom-right corner, with E at the centre
 * of the 5x5 neighbourhood:
 *
 *          B
 *       D  E  F  F4
 *       G  H  I  I4
 *          H5 I5      (and C above F)
 *
 * the edge is H-F when the differences along that diagonal (wd1) are smaller
 * than across it (wd2).  Straight edges score the same both ways and are left
 * sharp.  The other three corners are the same test mirrored, which is valid
 * because the test is symmetric about the E-I diagonal. */
static int xbr2x(Img *out, const Img *in)
{
    int x, y, c;

    out->w = in->w * 2;
    out->h = in->h * 2;
    out->px = (unsigned char *)malloc((size_t)out->w * out->h * 4);
    if (!out->px)
        return -1;
    for (y = 0; y < in->h; y++)
        for (x = 0; x < in->w; x++)
            for (c = 0; c < 4; c++) {
                int sx = (c & 1) ? 1 : -1, sy = (c & 2) ? 1 : -1;
                const unsigned char *E = at(in, x, y);
                const unsigned char *B = at(in, x, y - sy);
                const unsigned char *C = at(in, x + sx, y - sy);
                const unsigned char *D = at(in, x - sx, y);
                const unsigned char *F = at(in, x + sx, y);
                const unsigned char *G = at(in, x - sx, y + sy);
                const unsigned char *H = at(in, x, y + sy);
                const unsigned char *I = at(in, x + sx, y + sy);
                const unsigned char *F4 = at(in, x + 2 * sx, y);
                const unsigned char *I4 = at(in, x + 2 * sx, y + sy);
                const unsigned char *H5 = at(in, x, y + 2 * sy);
                const unsigned char *I5 = at(in, x + sx, y + 2 * sy);
                int wd1 = dist(E, C) + dist(E, G) + dist(I, F4) + dist(I, H5) +
                          4 * dist(H, F);
                int wd2 = dist(H, D) + dist(H, I5) + dist(F, I4) + dist(F, B) +
                          4 * dist(E, I);
                unsigned char *o = out->px +
                    ((size_t)(y * 2 + (sy > 0)) * out->w + x * 2 + (sx > 0)) * 4;
                int k;

                if (wd1 < wd2) {
                    const unsigned char *N = dist(E, F) <= dist(E, H) ? F : H;

                    for (k = 0; k < 4; k++)
                        o[k] = (unsigned char)((E[k] + N[k] + 1) / 2);
                } else {
                    memcpy(o, E, 4);
                }
            }
    return 0;
}

/* ------------------------------------------------------------- packing */

static const Dar *sort_dar;     /* for cmp_h; qsort has no context */

static int cmp_h(const void *a, const void *b)
{
    int ia = *(const int *)a, ib = *(const int *)b;
    int ha = ia < sort_dar->count ? sort_dar->pat[ia].h : sort_dar->pat[0].h;
    int hb = ib < sort_dar->count ? sort_dar->pat[ib].h : sort_dar->pat[0].h;

    return hb != ha ? hb - ha : ia - ib;
}

int atlas_build(Atlas *a, const Dar *d, const unsigned char *tint,
                int tintpat, int scale)
{
    int *order, i, x = 0, y = 0, shelf = 0, pad = PAD * scale;

    memset(a, 0, sizeof *a);
    a->scale = scale;
    a->n = d->count + 1;
    a->w = 1024 * scale;           /* the widest pattern is 460 */
    a->r = (AtlasRect *)calloc((size_t)a->n, sizeof *a->r);
    order = (int *)malloc((size_t)a->n * sizeof *order);
    if (!a->r || !order)
        goto fail;

    /* shelves, tallest first */
    for (i = 0; i < a->n; i++)
        order[i] = i;
    sort_dar = d;
    qsort(order, (size_t)a->n, sizeof *order, cmp_h);
    for (i = 0; i < a->n; i++) {
        int id = order[i], src = id < d->count ? id : tintpat;
        int w = d->pat[src].w * scale + 2 * pad, h = d->pat[src].h * scale + 2 * pad;

        if (x + w > a->w) {
            x = 0;
            y += shelf;
            shelf = 0;
        }
        a->r[id].x = x + pad;
        a->r[id].y = y + pad;
        a->r[id].w = w - 2 * pad;
        a->r[id].h = h - 2 * pad;
        x += w;
        if (h > shelf)
            shelf = h;
    }
    a->h = (y + shelf + 3) & ~3;

    a->rgba = (unsigned char *)calloc((size_t)a->w * a->h, 4);
    if (!a->rgba)
        goto fail;
    for (i = 0; i < a->n; i++) {
        int src = i < d->count ? i : tintpat, row;
        Img im, up;

        if (expand(&im, d, src, i < d->count ? NULL : tint) != 0)
            goto fail;
        for (; im.w < d->pat[src].w * scale; im = up) {
            if (xbr2x(&up, &im) != 0) {
                free(im.px);
                goto fail;
            }
            free(im.px);
        }
        for (row = 0; row < im.h; row++)
            memcpy(a->rgba + ((size_t)(a->r[i].y + row) * a->w + a->r[i].x) * 4,
                   im.px + (size_t)row * im.w * 4, (size_t)im.w * 4);
        free(im.px);
    }
    free(order);
    return 0;

fail:
    free(order);
    atlas_free(a);
    return -1;
}

void atlas_free(Atlas *a)
{
    free(a->rgba);
    free(a->r);
    memset(a, 0, sizeof *a);
}
