#!/bin/bash
set -e

# =====================================================================
#   Karamelo - Autonomous Recorder Build Script for macOS (Apple Silicon / arm64)
# =====================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/../.."

mkdir -p build/macos_record_obj
mkdir -p tools/record_gameplay/out

MACOS_MIN="11.0"
COMMON_INCLUDES="-Iinclude -Ithird_party/rcheevos/include -Ithird_party/rcheevos/src -Ithird_party/libchdr/include -Ithird_party/libchdr -Ithird_party/SDL3/include -Ithird_party/Vulkan-Headers/include -Ithird_party/libretro-common/include"
COMMON_DEFS="-D_DARWIN_C_SOURCE -DRC_CLIENT_SUPPORTS_HASH -DZSTD_DISABLE_ASM"

echo "[1/4] Compilando libchdr..."
clang -O2 -mmacosx-version-min=$MACOS_MIN -arch arm64 $COMMON_DEFS $COMMON_INCLUDES -c third_party/libchdr/unity.c -o build/macos_record_obj/unity.o

echo "[2/4] Compilando rcheevos..."
RCHEEVOS_SRCS=(
    third_party/rcheevos/src/*.c
    third_party/rcheevos/src/rapi/*.c
    third_party/rcheevos/src/rcheevos/*.c
    third_party/rcheevos/src/rhash/*.c
)
for src in ${RCHEEVOS_SRCS[@]}; do
    [ -f "$src" ] || continue
    obj="build/macos_record_obj/rc_$(basename "$src" .c).o"
    clang -O2 -mmacosx-version-min=$MACOS_MIN -arch arm64 $COMMON_DEFS $COMMON_INCLUDES -c "$src" -o "$obj"
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
    obj="build/macos_record_obj/$(basename "$src" .cpp).o"
    clang++ -std=c++20 -O2 -mmacosx-version-min=$MACOS_MIN -arch arm64 $COMMON_DEFS $COMMON_INCLUDES -c "$src" -o "$obj"
done

echo "[4/4] Linkando tools/record_gameplay/out/record_gameplay..."
SDL3_LIBS="build/sdl3_macos/libSDL3.a \
    -framework CoreMedia -framework CoreVideo -framework Cocoa -weak_framework UniformTypeIdentifiers \
    -framework IOKit -framework ForceFeedback -framework Carbon -framework CoreAudio -framework AudioToolbox \
    -framework AVFoundation -framework Foundation -framework GameController -framework Metal \
    -framework QuartzCore -weak_framework CoreHaptics"

clang++ build/macos_record_obj/*.o \
    $SDL3_LIBS \
    -ldl -lpthread \
    -mmacosx-version-min=$MACOS_MIN -arch arm64 \
    -o tools/record_gameplay/out/record_gameplay

echo "[BUILD SUCCESS] tools/record_gameplay/out/record_gameplay pronto para uso no macOS!"

