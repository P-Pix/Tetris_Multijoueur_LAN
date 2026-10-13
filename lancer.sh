#!/usr/bin/env sh
# Lance la version compilee depuis les sources ; aucun binaire ancien embarque.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ ! -x "$ROOT/build/tetris_lan" ]; then
    printf 'Premier lancement : compilation C++17 / SDL2...\n'
    "$ROOT/scripts/build.sh"
fi
exec "$ROOT/build/tetris_lan" "$@"
