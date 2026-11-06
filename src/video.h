/* The 8bpp back buffer WinDepth draws into, plus the dirty-rectangle list the
 * original keeps so it only blits the parts of the 512x384 surface that moved
 * (FUN_004025e0 collects rects, FUN_00402660 flushes them). */
#ifndef VIDEO_H
#define VIDEO_H

#include "dar.h"

#define SCR_W 512
#define SCR_H 384
#define MAX_DIRTY 0x400

typedef struct { int x, y, w, h; } Rect;

typedef struct {
    unsigned char px[SCR_W * SCR_H];
    Rect dirty[MAX_DIRTY];
    int ndirty;
    int full;              /* next present must push the whole surface */
    int ox, oy;            /* global draw offset (the library's shake) */
    const Dar *dar;
} Video;

void vid_init(Video *v, const Dar *dar);
void vid_clear(Video *v, unsigned char index);

/* Mark a rect for the next present.  Note the original adds its X offset to
 * BOTH coordinates here; harmless because the offset is always 0, and kept so
 * the behaviour is identical if it ever isn't. */
void vid_dirty(Video *v, int x, int y, int w, int h);
void vid_dirty_all(Video *v);

void vid_pat(Video *v, int x, int y, int pat);
void vid_pat_remap(Video *v, int x, int y, int pat, const unsigned char *lut);

/* Per-scanline sine displacement, the title screen's water ripple
 * (FUN_0040ea10).  `wave` is the original's wavelength argument: its magnitude
 * divides the row phase step and a negative value flips the amplitude on
 * alternate rows.  `amp` is the displacement in pixels, `phase` the scroll. */
void vid_pat_wave(Video *v, int x, int y, int pat, int wave, int amp, int phase);

/* Text: `bank` is 1..4, selecting one of the four 64-glyph WDFONTH banks.
 * Returns the number of characters drawn.  vid_text advances 8px per glyph,
 * vid_text_wide 12px (the original has both). */
int vid_text(Video *v, int x, int y, const char *s, int bank);
int vid_text_wide(Video *v, int x, int y, const char *s, int bank);

#endif
