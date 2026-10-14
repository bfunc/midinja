#!/bin/bash
# Builds the generator core to WebAssembly for the web preview: web/midinja-core.js
# (the .wasm is embedded, so the page needs no extra file). Needs Docker or emcc on PATH.
set -euo pipefail
cd "$(dirname "$0")/.."

SRC="src/core/Engine.cpp src/core/Generate.cpp web/Bindings.cpp"
FLAGS="-std=c++17 -O2 -Isrc/core -lembind -sMODULARIZE=1 -sEXPORT_NAME=MidinjaCore -sSINGLE_FILE=1 -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1"

if command -v emcc > /dev/null; then
    emcc $SRC $FLAGS -o web/midinja-core.js
else
    MSYS_NO_PATHCONV=1 docker run --rm -v "$(pwd -W 2> /dev/null || pwd):/src" -w /src emscripten/emsdk:3.1.74 \
        emcc $SRC $FLAGS -o web/midinja-core.js
fi
ls -l web/midinja-core.js
