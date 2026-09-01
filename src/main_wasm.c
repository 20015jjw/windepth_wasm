/* Emscripten front end.
 *
 * Same game core as the Win32 build; the only differences are where the
 * 8bpp surface goes and where input and sound come from:
 *
 *   - the surface is expanded through the DAR palette into an RGBA buffer and
 *     handed to the page, which does one putImageData per frame.  No WebGL and
 *     no 2D-canvas scaling tricks - the target machines have no working GL, so
 *     everything here rasterises in software.
 *   - the assets are baked into the module with --embed-file, so dar_load() and
 *     stage_load() keep using fopen() exactly as the native build does.
 *   - sound goes out through a tiny EM_JS hook; MIDI has no browser equivalent
 *     so wd_music is a no-op and the page can loop its own audio instead.
 */
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

#include "dar.h"
#include "video.h"
#include "game.h"

static Dar   g_dar;
static Video g_vid;
static Game  g_game;
static unsigned char g_rgba[SCR_W * SCR_H * 4];
static int g_ready;

EM_JS(void, js_sound, (int slot), {
    if (Module.wdSound)
        Module.wdSound(slot);
});

void wd_sound(int slot, int loop)
{
    (void)loop;
    js_sound(slot);
}

void wd_music(int which, int on)
{
    (void)which;
    (void)on;
}

/* Expand the indexed surface into the RGBA buffer the page blits. */
static void expand(void)
{
    unsigned *out = (unsigned *)g_rgba;
    const unsigned char *src = g_vid.px;
    unsigned lut[256];
    int i, n = SCR_W * SCR_H;

    for (i = 0; i < 256; i++)
        lut[i] = 0xff000000u | ((unsigned)g_dar.pal[i][2] << 16) |
                 ((unsigned)g_dar.pal[i][1] << 8) | g_dar.pal[i][0];
    for (i = 0; i < n; i++)
        out[i] = lut[src[i]];
}

EMSCRIPTEN_KEEPALIVE int wd_init(void)
{
    if (dar_load(&g_dar, "/data/windepth.dar") != 0)
        return -1;
    vid_init(&g_vid, &g_dar);
    game_init(&g_game, &g_vid, &g_dar, "/data/windepth.dat");
    g_ready = 1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int wd_width(void)  { return SCR_W; }
EMSCRIPTEN_KEEPALIVE int wd_height(void) { return SCR_H; }
EMSCRIPTEN_KEEPALIVE unsigned char *wd_framebuffer(void) { return g_rgba; }

/* The page owns the clock: it calls this every 50ms, matching the original's
 * timer, then draws whatever wd_framebuffer() points at. */
EMSCRIPTEN_KEEPALIVE void wd_tick(unsigned pad)
{
    if (!g_ready)
        return;
    g_game.pad = pad;
    game_tick(&g_game);
    g_vid.ndirty = 0;
    g_vid.full = 0;
    expand();
}

EMSCRIPTEN_KEEPALIVE void wd_start(void)    { if (g_ready) game_start(&g_game); }
EMSCRIPTEN_KEEPALIVE void wd_title(void)    { if (g_ready) game_title(&g_game); }
EMSCRIPTEN_KEEPALIVE int  wd_mode(void)     { return g_game.mode; }
EMSCRIPTEN_KEEPALIVE unsigned wd_score(void){ return g_game.score; }
EMSCRIPTEN_KEEPALIVE void wd_set_sfx(int on) { g_game.sfx_on = on ? 1 : 0; }

int main(void)
{
    return 0;   /* the page drives everything through the exports above */
}
