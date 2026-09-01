/* A minimal indexed-PNG writer for the test harnesses.
 *
 * Just enough of zlib to be valid: CRC-32, Adler-32 and a deflate stream made
 * only of stored (uncompressed) blocks.  Keeps the tests free of external
 * dependencies; the files are larger than they need to be but they are
 * throwaway.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned crc_tab[256];
static int crc_ready;

static void crc_init(void)
{
    unsigned n, k, c;

    for (n = 0; n < 256; n++) {
        c = n;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        crc_tab[n] = c;
    }
    crc_ready = 1;
}

static unsigned crc32x(unsigned crc, const unsigned char *b, size_t n)
{
    crc ^= 0xffffffffu;
    while (n--)
        crc = crc_tab[(crc ^ *b++) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

static unsigned adler32x(const unsigned char *b, size_t n)
{
    unsigned a = 1, s = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        a = (a + b[i]) % 65521u;
        s = (s + a) % 65521u;
    }
    return (s << 16) | a;
}

static unsigned char *zlib_store(const unsigned char *src, size_t n, size_t *out)
{
    size_t nblk = (n + 65534) / 65535, cap = 2 + nblk * 5 + n + 4, o = 0, i = 0;
    unsigned char *d = (unsigned char *)malloc(cap);
    unsigned ad = adler32x(src, n);

    d[o++] = 0x78; d[o++] = 0x01;
    while (i < n) {
        size_t len = n - i > 65535 ? 65535 : n - i;
        int last = (i + len == n);

        d[o++] = (unsigned char)last;
        d[o++] = (unsigned char)(len & 0xff);
        d[o++] = (unsigned char)(len >> 8);
        d[o++] = (unsigned char)(~len & 0xff);
        d[o++] = (unsigned char)((~len >> 8) & 0xff);
        memcpy(d + o, src + i, len);
        o += len;
        i += len;
    }
    d[o++] = (unsigned char)(ad >> 24); d[o++] = (unsigned char)(ad >> 16);
    d[o++] = (unsigned char)(ad >> 8);  d[o++] = (unsigned char)ad;
    *out = o;
    return d;
}

static void put32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static void chunk(FILE *f, const char *tag, const unsigned char *data, unsigned n)
{
    unsigned char hdr[8];
    unsigned crc;

    put32(hdr, n);
    memcpy(hdr + 4, tag, 4);
    fwrite(hdr, 1, 8, f);
    if (n)
        fwrite(data, 1, n, f);
    crc = crc32x(0, (const unsigned char *)tag, 4);
    if (n)
        crc = crc32x(crc, data, n);
    put32(hdr, crc);
    fwrite(hdr, 1, 4, f);
}

void png_indexed(const char *path, int w, int h, const unsigned char *px,
                 const unsigned char pal[256][3])
{
    FILE *f;
    unsigned char ihdr[13], plte[768];
    unsigned char *raw, *comp;
    size_t clen, rawlen = (size_t)(w + 1) * h;
    int y, i;

    if (!crc_ready)
        crc_init();
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    fwrite("\211PNG\r\n\032\n", 1, 8, f);
    put32(ihdr, (unsigned)w);
    put32(ihdr + 4, (unsigned)h);
    ihdr[8] = 8; ihdr[9] = 3; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    for (i = 0; i < 256; i++) {
        plte[i * 3 + 0] = pal[i][0];
        plte[i * 3 + 1] = pal[i][1];
        plte[i * 3 + 2] = pal[i][2];
    }
    chunk(f, "PLTE", plte, 768);

    raw = (unsigned char *)malloc(rawlen);
    for (y = 0; y < h; y++) {
        raw[(size_t)y * (w + 1)] = 0;
        memcpy(raw + (size_t)y * (w + 1) + 1, px + (size_t)y * w, (size_t)w);
    }
    comp = zlib_store(raw, rawlen, &clen);
    chunk(f, "IDAT", comp, (unsigned)clen);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(comp);
}
