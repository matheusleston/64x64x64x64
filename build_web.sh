#!/usr/bin/env bash
# Compila o núcleo + harness web para WebAssembly.
# Requer Emscripten (emcc) no PATH. Veja o README para instalar.
set -euo pipefail
cd "$(dirname "$0")"

emcc platform/web/main.cpp \
  -std=c++17 -O2 \
  -s MODULARIZE=1 -s EXPORT_NAME=createViz \
  -s ENVIRONMENT=web \
  -s EXPORTED_FUNCTIONS='["_viz_render","_viz_width","_viz_height","_viz_seed","_viz_info","_viz_setHueBase","_viz_toggleHueInvert","_viz_toggleHueCurve","_viz_setLumMax","_viz_newPattern","_viz_text","_viz_setTextVisible"]' \
  -s EXPORTED_RUNTIME_METHODS='["HEAPU8","UTF8ToString","stringToUTF8"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -o platform/web/viz.js

echo "OK -> platform/web/viz.js (+ viz.wasm)"
echo "Agora sirva a pasta e abra index.html, ex.:"
echo "  python3 -m http.server -d platform/web 8000"
