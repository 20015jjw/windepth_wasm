#!/bin/sh
# Build the WASM front end with emscripten, at low priority (tools/lowpri.sh).
#
# Output lands in the repository root because GitHub Pages serves main:/ - the
# page, the module and data/ all sit side by side there.
#
# windepth.dar, windepth.dat, windepth2p.dat and both .mid files are baked in with --embed-file
# so dar_load(), stage_load() and mus_load_file() keep using fopen() exactly as
# the native build does.  The WAV effects stay in data/ because the page decodes
# those itself through Web Audio; the MIDI is synthesised inside the module by
# src/synth.c, since a browser has no MIDI output.
set -e
cd "$(dirname "$0")/.."
EMSDK="${EMSDK:-/c/prog/emsdk/emsdk}"
EMCC="$EMSDK/upstream/emscripten/emcc.exe"
[ -f "$EMCC" ] || { echo "emcc not found at $EMCC" >&2; exit 1; }

EXPORTS=_main,_wd_init,_wd_tick,_wd_width,_wd_height,_wd_framebuffer
EXPORTS=$EXPORTS,_wd_start,_wd_stage,_wd_title,_wd_mode,_wd_score
EXPORTS=$EXPORTS,_wd_set_sfx,_wd_set_bgm
EXPORTS=$EXPORTS,_wd_audio_init,_wd_audio,_wd_audio_left,_wd_audio_right,_wd_audio_max
EXPORTS=$EXPORTS,_wd_set_modern,_wd_cmds,_wd_ncmds
EXPORTS=$EXPORTS,_wd_atlas_build,_wd_atlas_rgba,_wd_atlas_w,_wd_atlas_h,_wd_atlas_n
EXPORTS=$EXPORTS,_wd_atlas_rects,_wd_atlas_free

sh tools/lowpri.sh "$EMCC" -O2 -Wall -Wextra \
   -o windepth.js \
   src/main_wasm.c src/game.c src/stage.c src/video.c src/dar.c src/atlas.c \
   src/smf.c src/synth.c \
   --embed-file data/windepth.dar@/data/windepth.dar \
   --embed-file data/windepth.dat@/data/windepth.dat \
   --embed-file data/windepth2p.dat@/data/windepth2p.dat \
   --embed-file data/Windepth.mid@/data/Windepth.mid \
   --embed-file data/NAMEINN.MID@/data/NAMEINN.MID \
   -s MODULARIZE=0 -s EXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF32,HEAP32 \
   -s ALLOW_MEMORY_GROWTH=1 -s ENVIRONMENT=web,node \
   -s EXPORTED_FUNCTIONS="$EXPORTS"
echo "built windepth.js + windepth.wasm"
