/* Emscripten front end.
 *
 * Same game core as the Win32 build; the differences are where the 8bpp surface
 * goes and where input, sound and music come from:
 *
 *   - the surface is expanded through the DAR palette into an RGBA buffer and
 *     handed to the page, which does one putImageData per frame.  No WebGL and
 *     no 2D-canvas scaling tricks - the target machines have no working GL, so
 *     everything here rasterises in software.
 *   - the assets are baked into the module with --embed-file, so dar_load(),
 *     stage_load() and mus_load_file() keep using fopen() as the native build
 *     does.
 *   - effects go out through a tiny EM_JS hook so the page can play the WAVs.
 *   - music is synthesised here (src/synth.c) and pulled by the page's audio
 *     callback.  A browser has no MIDI output, and the native build's route -
 *     handing the file to the system synthesiser through MCI, which is what the
 *     original did through SMFDRV32.DLL - has no equivalent.
 */
#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

#include "dar.h"
#include "video.h"
#include "game.h"
#include "synth.h"

#define AUDIO_MAX 8192          /* frames per wd_audio call, at most */

static Dar   g_dar;
static Video g_vid;
static Game  g_game;
static unsigned char g_rgba[SCR_W * SCR_H * 4];
static int g_ready;

static Music g_mus;
static float g_al[AUDIO_MAX], g_ar[AUDIO_MAX];
static int g_mus_which = -1, g_mus_on;

EM_JS(void, js_sound, (int slot), {
    if (Module.wdSound)
        Module.wdSound(slot);
});

void wd_sound(int slot, int loop)
{
    (void)loop;
    js_sound(slot);
}

static void music_apply(void)
{
    if (!g_mus_on || g_mus_which < 0) {
        mus_stop(&g_mus);
        return;
    }
    if (mus_load_file(&g_mus, g_mus_which == 1 ? "/data/NAMEINN.MID"
                                              : "/data/Windepth.mid") == 0)
        mus_play(&g_mus, 1);
}

/* The game asks for a tune, not for a restart: the original only starts the
 * sequencer when it is not already running (FUN_00403030), so asking for the
 * tune that is already playing must not restart it. */
void wd_music(int which, int on)
{
    if (on && g_mus_on && which == g_mus_which)
        return;
    g_mus_which = which;
    g_mus_on = on;
    music_apply();
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
    mus_init(&g_mus, 44100);
    game_init(&g_game, &g_vid, &g_dar, "/data/windepth.dat");
    g_ready = 1;
    return 0;
}

EMSCRIPTEN_KEEPALIVE int wd_width(void)  { return SCR_W; }
EMSCRIPTEN_KEEPALIVE int wd_height(void) { return SCR_H; }
EMSCRIPTEN_KEEPALIVE unsigned char *wd_framebuffer(void) { return g_rgba; }

/* The page owns the clock: it calls this every 50ms, matching the original's
 * timer, then draws whatever wd_framebuffer() points at.  `pad2` is the second
 * co-op player; solo it only counts on the name entry screen. */
EMSCRIPTEN_KEEPALIVE void wd_tick(unsigned pad, unsigned pad2)
{
    if (!g_ready)
        return;
    g_game.pad = pad;
    g_game.pad2 = pad2;
    game_tick(&g_game);
    g_vid.ndirty = 0;
    g_vid.full = 0;
    expand();
}

/* players: 2 for co-op, anything else is solo */
EMSCRIPTEN_KEEPALIVE void wd_start(int players) { if (g_ready) game_start(&g_game, players); }

/* Jump the run in progress to another stage - the page's ?stage=N, for trying
 * a stage out without playing up to it. */
EMSCRIPTEN_KEEPALIVE void wd_stage(int no)
{
    if (g_ready && g_game.mode == MODE_PLAY && no >= 1 && no <= 20)
        game_stage(&g_game, no);
}

EMSCRIPTEN_KEEPALIVE void wd_title(void)     { if (g_ready) game_title(&g_game); }
EMSCRIPTEN_KEEPALIVE int  wd_mode(void)      { return g_game.mode; }
EMSCRIPTEN_KEEPALIVE unsigned wd_score(void) { return g_game.score; }
EMSCRIPTEN_KEEPALIVE void wd_set_sfx(int on) { g_game.sfx_on = on ? 1 : 0; }

EMSCRIPTEN_KEEPALIVE void wd_set_bgm(int on)
{
    g_game.bgm_on = on ? 1 : 0;
    wd_music(g_mus_which < 0 ? 0 : g_mus_which, g_game.bgm_on);
}

/* ---------------------------------------------------------------- audio */

/* Called once the page has an AudioContext, since only then is the real sample
 * rate known.  Re-inits the synth and resumes whatever tune was selected. */
EMSCRIPTEN_KEEPALIVE void wd_audio_init(int rate)
{
    mus_free(&g_mus);
    mus_init(&g_mus, rate);
    music_apply();
}

EMSCRIPTEN_KEEPALIVE float *wd_audio_left(void)  { return g_al; }
EMSCRIPTEN_KEEPALIVE float *wd_audio_right(void) { return g_ar; }
EMSCRIPTEN_KEEPALIVE int    wd_audio_max(void)   { return AUDIO_MAX; }

EMSCRIPTEN_KEEPALIVE void wd_audio(int frames)
{
    if (frames > AUDIO_MAX)
        frames = AUDIO_MAX;
    if (frames < 0)
        frames = 0;
    mus_render(&g_mus, g_al, g_ar, frames);
}

int main(void)
{
    return 0;   /* the page drives everything through the exports above */
}
