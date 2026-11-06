/* WIND_DAR - the pattern (sprite) archive WinDepth keeps in its "DAR" resource.
 *
 * Format recovered from FUN_0040f850 (header check) and FUN_0040f590 (record
 * walk) in the original binary:
 *
 *   0x000  char   magic[5]   "DAR:8"
 *   0x005  uint8  version    must be <= 1; WinDepth's blob is 1
 *   0x006  uint16 count      number of patterns (329)
 *   0x008  uint16 x2         unused here (4)
 *   0x00a  uint16 x3         unused here (0)
 *   0x00c  RGBQUAD pal[256]  B,G,R,0 - a Windows DIB colour table
 *   0x40c  count records:
 *            uint16 reclen     bytes from just after this field to the pixels
 *            uint16 w          width in pixels
 *            uint16 h          rows of pixel data
 *            uint16 pitch      bytes per stored row
 *            int16  trans      transparent palette index, -1 = opaque
 *            [reclen>13]  uint16 ox, uint16 oy, uint16 h2  (all 0 / == h here)
 *            [reclen>15]  uint8 nfield, char name[nfield-1]  (NUL-padded)
 *            pixels: h * pitch bytes
 *
 * A stored row is a run list, not a raw scanline:
 *
 *   uint16 gap;        transparent pixels to skip
 *   uint16 len;        opaque pixels that follow; 0 ends the row
 *   uint8  px[len];    padded so the next header starts on a 4-byte boundary
 *
 * The row also ends once the runs have covered `w` columns, which is why the
 * fully opaque backgrounds carry no terminator.  Across all 329 patterns and
 * 6543 rows the runs cover exactly `w` every time.
 *
 * `pitch` is the largest a row gets, not the size of any particular row, which
 * is why it looks unrelated to `w` - a sparse sprite like BIOLOGO1 is 366 wide
 * but never needs more than 160 bytes in one row.
 *
 * Rows are stored BOTTOM-UP, matching the Windows DIB the original blits from
 * (its blit setup walks the destination with a negative stride).  Read a
 * pattern top-down and every sprite carrying text comes out mirrored, which is
 * the giveaway.
 */
#ifndef DAR_H
#define DAR_H

typedef struct {
    int w, h;                  /* pixel size */
    int pitch;                 /* stride of the stored rows */
    int trans;                 /* transparent index, -1 = none */
    const unsigned char *data; /* h rows of `pitch` bytes */
    char name[12];
} DarPat;

typedef struct {
    unsigned char *blob;       /* owned copy of the whole archive */
    long blob_size;
    int count;
    DarPat *pat;               /* count patterns */
    unsigned char pal[256][3]; /* r,g,b */
} Dar;

/* Parse a DAR image already in memory.  Takes ownership of `blob` on success
 * (it is freed by dar_free).  Returns 0 on success, -1 on a malformed blob. */
int  dar_open(Dar *d, unsigned char *blob, long size);
/* Read a .dar file from disk and dar_open it. */
int  dar_load(Dar *d, const char *path);
void dar_free(Dar *d);

/* Look a pattern up by name; -1 when there is no such pattern. */
int  dar_find(const Dar *d, const char *name);

/* Expand one stored row into `out[w]`, writing the pattern's transparent index
 * into the gaps (or leaving them untouched when `keep` is set).  Used by the
 * blitters and by the asset dumper. */
void dar_row(const DarPat *p, int row, unsigned char *out);

/* Draw pattern `id` with its top-left at (x,y) on an 8bpp surface, clipped to
 * the surface and skipping the pattern's transparent index. */
void dar_blit(const Dar *d, int id, unsigned char *dst, int dpitch,
              int dw, int dh, int x, int y);

/* As dar_blit, but every opaque pixel goes through `lut` first - how the
 * second co-op ship gets its own colours out of the one destroyer sprite. */
void dar_blit_remap(const Dar *d, int id, unsigned char *dst, int dpitch,
                    int dw, int dh, int x, int y, const unsigned char *lut);

/* As dar_blit, but each row is shifted horizontally by dx[row] - the title
 * screen's water ripple.  `dx` must have at least the pattern's height. */
void dar_blit_rowshift(const Dar *d, int id, unsigned char *dst, int dpitch,
                       int dw, int dh, int x, int y, const int *dx);

#endif
