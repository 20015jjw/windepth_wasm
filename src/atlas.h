/* Every DAR pattern packed into one RGBA image, for a front end that draws
 * the frame on the GPU from the recorded draws (video.h's VidCmd) instead of
 * taking the 8bpp surface.
 *
 * Pixels are premultiplied RGBA through the DAR palette, with the pattern's
 * transparent index as (0,0,0,0).  At scale 1 the patterns are copied as
 * they are; at 2 or 4 each is upscaled by an xBR-style 2x edge filter, once or
 * twice, for the smoothed look.  Every pattern has transparent padding
 * around it so a filtering sampler never picks up a neighbour.
 */
#ifndef ATLAS_H
#define ATLAS_H

#include "dar.h"

typedef struct { int x, y, w, h; } AtlasRect;

typedef struct {
    int scale;
    int w, h;
    unsigned char *rgba;   /* w * h * 4 */
    int n;                 /* d->count + 1: the last is the tinted ship */
    AtlasRect *r;          /* where each pattern is, in atlas pixels */
} Atlas;

/* `tint` remaps pattern `tintpat` into the extra last entry - ship 1's
 * colours, the only remap the game draws with (VC_TINT).  Returns 0, or -1
 * when out of memory. */
int  atlas_build(Atlas *a, const Dar *d, const unsigned char *tint,
                 int tintpat, int scale);
void atlas_free(Atlas *a);

#endif
