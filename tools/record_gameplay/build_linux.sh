#!/bin/bash
set -e

# =====================================================================
#   Karamelo - Autonomous Recorder Build Script for Linux (x86_64)
# =====================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/../.."

mkdir -p build/linux_record_obj
mkdir -p tools/record_gameplay/out

COMMON_INCLUDES="-Iinclude -Ithird_party/rcheevos/include -Ithird_party/rcheevos/src -Ithird_party/libchdr/include -Ithird_party/libchdr -Ithird_party/SDL3/include -Ithird_party/Vulkan-Headers/include -Ithird_party/libretro-common/include"
COMMON_DEFS="-D_GNU_SOURCE -DRC_CLIENT_SUPPORTS_HASH -DZSTD_DISABLE_ASM"

echo "[1/4] Compilando libchdr..."
gcc -O2 $COMMON_DEFS $COMMON_INCLUDES -c third_party/libchdr/unity.c -o build/linux_record_obj/unity.o

echo "[2/4] Compilando rcheevos..."
RCHEEVOS_SRCS=(
    third_party/rcheevos/src/*.c
    third_party/rcheevos/src/rapi/*.c
    third_party/rcheevos/src/rcheevos/*.c
    third_party/rcheevos/src/rhash/*.c
)
for src in ${RCHEEVOS_SRCS[@]}; do
    [ -f "$src" ] || continue
    obj="build/linux_record_obj/rc_$(basename "$src" .c).o"
    gcc -O2 $COMMON_DEFS $COMMON_INCLUDES -c "$src" -o "$obj"
done

echo "[3/4] Compilando modulos C++20..."
SRCS=(
    src/karamelo_math.cpp
    src/input_map.cpp
    src/gamepad_sdl.cpp
    src/hw_render.cpp
    src/hw_render_vulkan.cpp
    src/hw_render_d3d11.cpp
    src/chd_reader.cpp
    src/core_runner.cpp
    src/archive_helper.cpp
    tools/record_gameplay/headless_stubs.cpp
    tools/record_gameplay/record_gameplay.cpp
)

for src in "${SRCS[@]}"; do
    obj="build/linux_record_obj/$(basename "$src" .cpp).o"
    g++ -std=c++20 -O2 $COMMON_DEFS $COMMON_INCLUDES -c "$src" -o "$obj"
done

echo "[4/4] Linkando tools/record_gameplay/out/record_gameplay..."
SDL3_LIB="build/sdl3_linux/libSDL3.a"
if [ ! -f "$SDL3_LIB" ]; then
    SDL3_LIB="-lSDL3"
fi

g++ build/linux_record_obj/*.o \
    "$SDL3_LIB" \
    -ldl -lpthread -lm -lGL \
    -o tools/record_gameplay/out/record_gameplay

echo "[BUILD SUCCESS] tools/record_gameplay/out/record_gameplay pronto para uso!"

