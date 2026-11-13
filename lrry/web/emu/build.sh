#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Felucca in the browser: the firmware's sources as the host tests build them (web/emu/lrry_web.c), compiled
# to WebAssembly with Emscripten, plus the page. After X0X's web/emu/build.sh (charlesvestal/fm1-x0x, GPL-3.0).
#   web/emu/build.sh   ->  build/emu/{index.html, worklet.js, lrry.wasm, fonts/}
# Run after one ./build.sh (build/gen: the tables, the fonts, the factory sounds and multis).
# -ffp-contract=off: no fused multiply-adds, as the host build. EMU_OPT replaces -O2, EMU_CFLAGS adds flags
# (EMU_CFLAGS=-msimd128: let the compiler vectorise; every browser with AudioWorklet has wasm SIMD).
# LRRY_TEST_MODE=1 builds in the CPU TEST screen (default 0, the release; since 0.2.23 both hold the same sixteen factory multis; build/gen/lrry_multis.h must come from the same
# switch: LRRY_TEST_MODE=1 python3 tools/gen_lrry_multis.py build/gen/lrry_multis.h, or LRRY/gen_only.py with it set; multi.c refuses a mismatch).
# The page shows the version (LRRY_VERSION_STR, from the VERSION file) marked "(preview)" before its release; EMU_PREVIEW=0 drops the mark.
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/lrry_tables.h ] || { echo "emu: run ./build.sh once first (build/gen)"; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten)"; exit 1; }
python3 tools/gen_version.py build/gen/lrry_version.h     # the VERSION file at the L/RRY root
VERSION=$(sed -n 's/^#define LRRY_VERSION_STR "\(.*\)"/\1/p' build/gen/lrry_version.h)
mkdir -p build/emu/fonts
# shellcheck disable=SC2086
emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w ${EMU_CFLAGS:-} \
    "-DLRRY_VERSION_SHOWN=\"$VERSION\"" "-DLRRY_TEST_MODE=${LRRY_TEST_MODE:-0}" -Ibuild/gen -Ifirmware/src \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/emu/lrry.wasm web/emu/lrry_web.c
[ "${EMU_PREVIEW:-1}" = 0 ] && PREVIEW='' || PREVIEW=' (preview)'
sed -e "s|@VERSION@|$VERSION|g" -e "s|@PREVIEW@|$PREVIEW|g" web/emu/index.html > build/emu/index.html
cp web/emu/worklet.js build/emu/
cp web/emu/fonts/DotGothic16-subset.woff web/emu/fonts/OFL.txt build/emu/fonts/
echo "emu: build/emu ($(wc -c < build/emu/lrry.wasm | tr -d ' ') B wasm, $VERSION)"
