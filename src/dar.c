#include "dar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }

int dar_open(Dar *d, unsigned char *blob, long size)
{
    long o;
    int i;

    memset(d, 0, sizeof *d);
    if (size < 0x40c || memcmp(blob, "DAR:8", 5) != 0 || blob[5] > 1)
        return -1;
    d->blob = blob;
    d->blob_size = size;
    d->count = (int)rd16(blob + 6);
    for (i = 0; i < 256; i++) {
        d->pal[i][0] = blob[0x0c + 4 * i + 2];   /* stored B,G,R,0 */
        d->pal[i][1] = blob[0x0c + 4 * i + 1];
        d->pal[i][2] = blob[0x0c + 4 * i + 0];
    }
    d->pat = (DarPat *)calloc((size_t)d->count, sizeof *d->pat);
    if (!d->pat)
        return -1;

    o = 0x40c;
    for (i = 0; i < d->count; i++) {
        DarPat *p = &d->pat[i];
        long b, need;
        int reclen;

        if (o + 2 > size)
            return -1;
        reclen = (int)rd16(blob + o);
        b = o + 2;
        if (reclen < 8 || b + reclen > size)
            return -1;
        p->w     = (int)rd16(blob + b + 0);
        p->h     = (int)rd16(blob + b + 2);
        p->pitch = (int)rd16(blob + b + 4);
        p->trans = (short)rd16(blob + b + 6);
        if (reclen > 15) {
            int nf = blob[b + 14], k;

            for (k = 0; k < nf - 1 && k < (int)sizeof p->name - 1; k++) {
                unsigned char c = blob[b + 15 + k];

                if (!c)
                    break;
                p->name[k] = (char)c;
            }
        }
        p->data = blob + b + reclen;
        need = (long)p->h * p->pitch;
        if (b + reclen + need > size)
            return -1;
        o = b + reclen + need;
    }
    return 0;
}

int dar_load(Dar *d, const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long size;

    memset(d, 0, sizeof *d);
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    if (dar_open(d, buf, size) != 0) {
        free(buf);
        free(d->pat);
        memset(d, 0, sizeof *d);
        return -1;
    }
    return 0;
}

void dar_free(Dar *d)
{
    free(d->blob);
    free(d->pat);
    memset(d, 0, sizeof *d);
}

int dar_find(const Dar *d, const char *name)
{
    int i;

    for (i = 0; i < d->count; i++)
        if (strcmp(d->pat[i].name, name) == 0)
            return i;
    return -1;
}

void dar_row(const DarPat *p, int row, unsigned char *out)
{
    /* Stored bottom-up, like the Windows DIB the original blits from. */
    const unsigned char *r = p->data + (long)(p->h - 1 - row) * p->pitch;
    unsigned char fill = p->trans < 0 ? 0 : (unsigned char)p->trans;
    int q = 0, x = 0;

    memset(out, fill, (size_t)p->w);
    while (q + 4 <= p->pitch) {
        int gap = (int)rd16(r + q), len = (int)rd16(r + q + 2);

        x += gap;
        if (len == 0 || x >= p->w)
            break;
        if (x + len > p->w)
            len = p->w - x;
        memcpy(out + x, r + q + 4, (size_t)len);
        x += len;
        q += 4 + ((len + 3) & ~3);
        if (x >= p->w)
            break;
    }
}

/* Walk one row's runs, handing each opaque span to the caller.  Kept as a
 * macro-free helper so both blitters share exactly one decoder. */
typedef void (*SpanFn)(void *ctx, int x, int len, const unsigned char *px);

static void row_spans(const DarPat *p, int row, SpanFn fn, void *ctx)
{
    const unsigned char *r = p->data + (long)(p->h - 1 - row) * p->pitch;
    int q = 0, x = 0;

    while (q + 4 <= p->pitch) {
        int gap = (int)rd16(r + q), len = (int)rd16(r + q + 2);

        x += gap;
        if (len == 0 || x >= p->w)
            return;
        if (x + len > p->w)
            len = p->w - x;
        fn(ctx, x, len, r + q + 4);
        x += len;
        q += 4 + ((len + 3) & ~3);
        if (x >= p->w)
            return;
    }
}

typedef struct {
    unsigned char *row;   /* destination scanline base (already offset by y) */
    int x0;               /* where pattern column 0 lands */
    int clip_lo, clip_hi; /* inclusive-exclusive destination window */
    int trans;
    const unsigned char *lut;  /* palette remap, or NULL */
} BlitCtx;

static void blit_span(void *vctx, int sx, int len, const unsigned char *px)
{
    BlitCtx *c = (BlitCtx *)vctx;
    int cx = c->x0 + sx, i = 0;

    if (cx < c->clip_lo) {
        i = c->clip_lo - cx;
        if (i >= len)
            return;
    }
    if (cx + len > c->clip_hi)
        len = c->clip_hi - cx;
    if (len <= i)
        return;
    if (c->lut) {
        unsigned char *out = c->row + cx;

        for (; i < len; i++)
            if ((int)px[i] != c->trans)
                out[i] = c->lut[px[i]];
    } else if (c->trans < 0) {
        memcpy(c->row + cx + i, px + i, (size_t)(len - i));
    } else {
        unsigned char tr = (unsigned char)c->trans;
        unsigned char *out = c->row + cx;

        for (; i < len; i++)
            if (px[i] != tr)
                out[i] = px[i];
    }
}

void dar_blit_remap(const Dar *d, int id, unsigned char *dst, int dpitch,
                    int dw, int dh, int x, int y, const unsigned char *lut)
{
    const DarPat *p;
    int row;

    if (id < 0 || id >= d->count)
        return;
    p = &d->pat[id];
    for (row = 0; row < p->h; row++) {
        BlitCtx c;
        int dy = y + row;

        if (dy < 0 || dy >= dh)
            continue;
        c.row = dst + (long)dy * dpitch;
        c.x0 = x;
        c.clip_lo = 0;
        c.clip_hi = dw;
        c.trans = p->trans;
        c.lut = lut;
        row_spans(p, row, blit_span, &c);
    }
}

void dar_blit(const Dar *d, int id, unsigned char *dst, int dpitch,
              int dw, int dh, int x, int y)
{
    dar_blit_remap(d, id, dst, dpitch, dw, dh, x, y, NULL);
}

void dar_blit_rowshift(const Dar *d, int id, unsigned char *dst, int dpitch,
                       int dw, int dh, int x, int y, const int *dx)
{
    const DarPat *p;
    int row;

    if (id < 0 || id >= d->count)
        return;
    p = &d->pat[id];
    for (row = 0; row < p->h; row++) {
        BlitCtx c;
        int dy = y + row;

        if (dy < 0 || dy >= dh)
            continue;
        c.row = dst + (long)dy * dpitch;
        c.x0 = x + dx[row];
        c.clip_lo = 0;
        c.clip_hi = dw;
        c.trans = p->trans;
        c.lut = NULL;
        row_spans(p, row, blit_span, &c);
    }
}
