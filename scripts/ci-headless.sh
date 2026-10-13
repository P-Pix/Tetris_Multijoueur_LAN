#!/usr/bin/env sh
# Compilation CI sans fenetre ni paquet libsdl2-dev : coeur + protocole + TCP.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cmake -S "$ROOT" -B "$ROOT/build-ci" -DCMAKE_BUILD_TYPE=Debug -DTETRIS_BUILD_GUI=OFF
cmake --build "$ROOT/build-ci" --parallel 2
ctest --test-dir "$ROOT/build-ci" --output-on-failure
