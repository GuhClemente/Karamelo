#!/bin/bash
set -e

# ===========================================================================
#  build_hikaru.sh - compila o core de Sega Hikaru (Odin) para Karamelo
#
#  Baseado no emulador Odin de Reda Cherif-Touil (Tovarichtch) e Stefano Teso (Valkyrie).
#  Licenca: GNU GPL-3.0
#
#  Este script compila o wrapper libretro em third_party/odin_libretro
#  e gera cores/hikaru.dylib (macOS) ou cores/hikaru.so (Linux).
# ===========================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

UNAME_S="$(uname -s)"
if [ "$UNAME_S" = "Darwin" ]; then
    CORE_EXT="dylib"
else
    CORE_EXT="so"
fi

echo "[HIKARU] Configurando CMake em third_party/odin/build..."
cmake -B third_party/odin/build -S third_party/odin -G Ninja -DODIN_BUNDLED_LIBS=ON

echo "[HIKARU] Compilando cores/hikaru.${CORE_EXT} via Ninja..."
ninja -C third_party/odin/build hikaru

mkdir -p cores app/cores data

cp "third_party/odin/build/hikaru.${CORE_EXT}" "cores/hikaru.${CORE_EXT}"
cp "third_party/odin/build/hikaru.${CORE_EXT}" "app/cores/hikaru.${CORE_EXT}"

if [ -f third_party/odin/build/share/odin/odin-games.json ]; then
    cp third_party/odin/build/share/odin/odin-games.json data/odin-games.json
    cp third_party/odin/build/share/odin/odin-games.json app/odin-games.json
    cp third_party/odin/build/share/odin/odin-games.json cores/odin-games.json
fi

echo "[HIKARU] Sucesso! cores/hikaru.${CORE_EXT} e app/cores/hikaru.${CORE_EXT} atualizados."

