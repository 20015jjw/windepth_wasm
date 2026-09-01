#!/bin/sh
# Build the native Win32 build with mingw, at low priority (tools/lowpri.sh).
set -e
cd "$(dirname "$0")/.."
GCC="${GCC:-/c/prog/w64devkit/bin/gcc}"
SRC="src/main_win32.c src/game.c src/stage.c src/video.c src/dar.c"
sh tools/lowpri.sh "$GCC" -O2 -Wall -Wextra -mwindows -o windepth.exe $SRC -lgdi32 -lwinmm
echo "built windepth.exe"
