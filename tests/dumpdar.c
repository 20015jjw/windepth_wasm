/* Dump every DAR pattern as a PNG, using the port's own decoder - so the
 * dumps and the game always agree about the format. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/dar.h"
extern void png_indexed(const char *path, int w, int h, const unsigned char *px,
                        const unsigned char pal[256][3]);
int main(int argc, char **argv)
{
    Dar d;
    int i;
    const char *dir = argc > 1 ? argv[1] : "tests/dar2";
    if (dar_load(&d, "data/windepth.dar") != 0) { fprintf(stderr,"no dar\n"); return 1; }
    for (i = 0; i < d.count; i++) {
        const DarPat *p = &d.pat[i];
        unsigned char *px = malloc((size_t)p->w * p->h);
        char path[512];
        int y;
        for (y = 0; y < p->h; y++)
            dar_row(p, y, px + (size_t)y * p->w);
        sprintf(path, "%s/%03d_%s.png", dir, i, p->name);
        png_indexed(path, p->w, p->h, px, d.pal);
        free(px);
    }
    printf("dumped %d patterns to %s\n", d.count, dir);
    dar_free(&d);
    return 0;
}
