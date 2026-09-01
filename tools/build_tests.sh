#!/bin/sh
# Build the headless harnesses, at low priority (tools/lowpri.sh).
set -e
cd "$(dirname "$0")/.."
GCC="${GCC:-/c/prog/w64devkit/bin/gcc}"
CORE="src/game.c src/stage.c src/video.c src/dar.c tests/png.c"
sh tools/lowpri.sh "$GCC" -O2 -Wall -Wextra -o tests/frames.exe tests/frames.c $CORE
sh tools/lowpri.sh "$GCC" -O2 -Wall -Wextra -o tests/dumpdar.exe tests/dumpdar.c src/dar.c tests/png.c
echo "built tests/frames.exe tests/dumpdar.exe"
