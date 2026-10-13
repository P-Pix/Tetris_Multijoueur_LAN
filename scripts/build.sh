#!/usr/bin/env sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cmake -S "$HERE" -B "$HERE/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$HERE/build" --parallel 2
ctest --test-dir "$HERE/build" --output-on-failure
printf '\nJeu pret : %s\n' "$HERE/build/tetris_lan"
