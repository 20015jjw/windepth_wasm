#!/bin/sh
# Build the WASM front end with emscripten, at low priority (tools/lowpri.sh).
#
# Output lands in the repository root because GitHub Pages serves main:/ - the
# page, the module and data/ all sit side by side there.
#
# windepth.dar and windepth.dat are baked in with --embed-file so dar_load()
# and stage_load() keep using fopen() unchanged; the WAV files stay in data/
# because the page decodes them itself through Web Audio.
set -e
cd "$(dirname "$0")/.."
EMSDK="${EMSDK:-/c/prog/emsdk/emsdk}"
EMCC="$EMSDK/upstream/emscripten/emcc.exe"
[ -f "$EMCC" ] || { echo "emcc not found at $EMCC" >&2; exit 1; }
sh tools/lowpri.sh "$EMCC" -O2 -Wall -Wextra \
   -o windepth.js \
   src/main_wasm.c src/game.c src/stage.c src/video.c src/dar.c \
   --embed-file data/windepth.dar@/data/windepth.dar \
   --embed-file data/windepth.dat@/data/windepth.dat \
   -s MODULARIZE=0 -s EXPORTED_RUNTIME_METHODS=HEAPU8 \
   -s ALLOW_MEMORY_GROWTH=1 -s ENVIRONMENT=web,node \
   -s EXPORTED_FUNCTIONS=_main,_wd_init,_wd_tick,_wd_width,_wd_height,_wd_framebuffer,_wd_start,_wd_title,_wd_mode,_wd_score,_wd_set_sfx
echo "built windepth.js + windepth.wasm"
