#!/bin/sh
# Baut den Klang-Kern der Onno-Suite als WebAssembly (für die Sweet-Disaster-DAW und andere Web-Apps).
# Voraussetzung: Emscripten (emsdk), `source emsdk_env.sh`.
set -e
cd "$(dirname "$0")"
mkdir -p dist
em++ -O3 -std=c++17 -fno-exceptions -fno-rtti -I ../Source onno_wasm.cpp \
  -sSTANDALONE_WASM=1 --no-entry -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=4mb -sSTACK_SIZE=256kb \
  -sEXPORTED_FUNCTIONS=_onno_kinds,_onno_kind_id,_onno_params_json,_onno_create,_onno_destroy,_onno_param_count,_onno_set,_onno_get,_onno_buffer,_onno_latency,_onno_process,_onno_meter,_onno_eq_response \
  -o dist/onno-dsp.wasm
ls -l dist/onno-dsp.wasm
