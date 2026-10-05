#!/bin/bash
set -e

# =====================================================================
#   Karamelo - Build Script para macOS (Apple Silicon / Intel)
# =====================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

ARCH=$(uname -m)
echo "====================================================================="
echo "  Karamelo - macOS Build Pipeline ($ARCH)"
echo "====================================================================="

mkdir -p build/macos_obj
mkdir -p app

# 1. Obter Versao
APP_VER=$(grep "#define APP_VERSION " include/app_info.h | awk '{print $3}' | tr -d '"')
echo "[INFO] Versao do Karamelo: $APP_VER"

# 2. SDL3 estatico, compilado de third_party/SDL3 (o mesmo que o Linux usa)
# O binario linkava o libSDL3.0.dylib do Homebrew por caminho absoluto: num Mac
# sem "brew install sdl3" o Karamelo nem abria, e o dylib do Homebrew exige a
# versao de macOS da maquina que o compilou. Estatico, o executavel se basta -
# e e o unico arquivo que o auto-update troca.
MACOS_MIN="11.0"   # primeira versao do macOS em Apple Silicon; os cores tambem pedem 11.0
if [ ! -f "build/sdl3_macos/libSDL3.a" ]; then
    echo "[SDL3] Configurando e compilando libSDL3.a estatica (macOS $MACOS_MIN+)..."
    cmake -S third_party/SDL3 -B build/sdl3_macos -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOS_MIN \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DSDL_STATIC=ON \
        -DSDL_SHARED=OFF \
        -DSDL_TEST_LIBRARY=OFF \
        -DSDL_EXAMPLES=OFF \
        -DSDL_DISABLE_INSTALL=ON \
        -DSDL_DISABLE_INSTALL_DOCS=ON
    cmake --build build/sdl3_macos --config Release -j$(sysctl -n hw.ncpu)
fi
echo "[INFO] SDL3 estatico pronto em build/sdl3_macos/libSDL3.a"

# Frameworks que o libSDL3.a estatico precisa (Libs do sdl3.pc gerado pelo cmake)
SDL3_LIBS="build/sdl3_macos/libSDL3.a \
    -framework CoreMedia -framework CoreVideo -framework Cocoa -weak_framework UniformTypeIdentifiers \
    -framework IOKit -framework ForceFeedback -framework Carbon -framework CoreAudio -framework AudioToolbox \
    -framework AVFoundation -framework Foundation -framework GameController -framework Metal \
    -framework QuartzCore -weak_framework CoreHaptics"

# Flags comuns
COMMON_INCLUDES="-Iinclude -Ithird_party/rcheevos/include -Ithird_party/rcheevos/src -Ithird_party/libchdr/include -Ithird_party/libchdr -Ithird_party/SDL3/include -Ithird_party/Vulkan-Headers/include -Ithird_party/libretro-common/include"
COMMON_DEFS="-mmacosx-version-min=$MACOS_MIN -DRC_CLIENT_SUPPORTS_HASH -DZSTD_DISABLE_ASM -DGL_SILENCE_DEPRECATION"

# 3. Compilar libchdr (C)
if [ ! -f "build/macos_obj/chdr_unity.o" ] || [ "third_party/libchdr/unity.c" -nt "build/macos_obj/chdr_unity.o" ]; then
    echo "[1/4] Compilando libchdr..."
    clang -O2 $COMMON_DEFS $COMMON_INCLUDES -c third_party/libchdr/unity.c -o build/macos_obj/chdr_unity.o
else
    echo "[1/4] libchdr ja compilado."
fi

# 4. Compilar rcheevos (C)
echo "[2/4] Verificando e compilando rcheevos..."
RCHEEVOS_SRCS=(
    third_party/rcheevos/src/*.c
    third_party/rcheevos/src/rapi/*.c
    third_party/rcheevos/src/rcheevos/*.c
    third_party/rcheevos/src/rhash/*.c
)
for src in ${RCHEEVOS_SRCS[@]}; do
    [ -f "$src" ] || continue
    obj="build/macos_obj/rc_$(basename "$src" .c).o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        echo "  Compilando $src..."
        clang -O2 $COMMON_DEFS $COMMON_INCLUDES -c "$src" -o "$obj"
    fi
done

# 5. Compilar Modulos Karamelo (C++20)
echo "[3/4] Verificando e compilando fontes C++ do Karamelo..."
KARAMELO_SRCS=(
    src/karamelo_math.cpp
    src/input_map.cpp
    src/gamepad_sdl.cpp
    src/hw_render.cpp
    src/hw_render_vulkan.cpp
    src/hw_render_d3d11.cpp
    src/chd_reader.cpp
    src/netplay_protocol.cpp
    src/charrom.cpp
    src/osd.cpp
    src/menu.cpp
    src/core_runner.cpp
    src/archive_helper.cpp
    src/netplay.cpp
    src/retroachievements.cpp
    src/updater.cpp
    src/port_runner.cpp
    src/x360_dump.cpp
    src/crash_reporter.cpp
    src/main_linux.cpp
)

for src in "${KARAMELO_SRCS[@]}"; do
    obj="build/macos_obj/$(basename "$src" .cpp).o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        echo "  Compilando $src..."
        clang++ -std=c++20 -O2 $COMMON_DEFS $COMMON_INCLUDES -c "$src" -o "$obj"
    fi
done

# 6. Linkar Executavel Principal
echo "[4/4] Linkando app/Karamelo..."
clang++ -std=c++20 -mmacosx-version-min=$MACOS_MIN build/macos_obj/*.o \
    $SDL3_LIBS \
    -framework OpenGL \
    -lpthread -ldl -lm \
    -o app/Karamelo

echo "====================================================================="
echo "  [SUCESSO] app/Karamelo construido com sucesso para macOS ($ARCH)!"
cp -f app/Karamelo app/karamelo 2>/dev/null || true
ln -sf app/Karamelo karamelo 2>/dev/null || true
ls -lh app/Karamelo
file app/Karamelo
echo "====================================================================="

# 7. Compilar e rodar a suite de testes unitarios
echo "[TESTES] Compilando e executando testes unitarios..."
clang++ -std=c++20 -O2 $COMMON_DEFS $COMMON_INCLUDES \
    tests/test_main.cpp \
    tests/test_config.cpp \
    tests/test_archive.cpp \
    tests/test_netplay.cpp \
    tests/test_math_video.cpp \
    tests/test_updater.cpp \
    tests/test_crash_reporter.cpp \
    build/macos_obj/karamelo_math.o \
    build/macos_obj/netplay_protocol.o \
    build/macos_obj/archive_helper.o \
    build/macos_obj/updater.o \
    build/macos_obj/crash_reporter.o \
    -lpthread -ldl \
    -o build/karamelo_tests

./build/karamelo_tests
