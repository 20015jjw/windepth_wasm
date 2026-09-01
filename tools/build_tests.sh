#!/bin/sh
# Build the headless harnesses, at low priority (tools/lowpri.sh).
#
#   tests/frames.exe    run the game N ticks and dump the surface as PNGs
#   tests/dumpdar.exe   dump all 329 DAR patterns as PNGs
#   tests/midi2wav.exe  render a .mid with the port's synth to a WAV
set -e
cd "$(dirname "$0")/.."
GCC="${GCC:-/c/prog/w64devkit/bin/gcc}"
CORE="src/game.c src/stage.c src/video.c src/dar.c tests/png.c"
sh tools/lowpri.sh "$GCC" -O2 -Wall -Wextra -o tests/frames.exe tests/frames.c $CORE
sh tools/lowpri.sh "$GCC" -O2 -Wall -Wextra -o tests/dumpdar.exe \
   tests/dumpdar.c src/dar.c tests/png.c
sh tools/lowpri.sh "$GCC" -O2 -Wall -Wextra -o tests/midi2wav.exe \
   tests/midi2wav.c src/smf.c src/synth.c -lm
echo "built tests/frames.exe tests/dumpdar.exe tests/midi2wav.exe"
