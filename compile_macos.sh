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

# 2. Detectar SDL3 (Homebrew ou pkg-config)
SDL3_CFLAGS=""
SDL3_LIBS=""
if pkg-config --exists sdl3 2>/dev/null; then
    SDL3_CFLAGS=$(pkg-config --cflags sdl3)
    SDL3_LIBS=$(pkg-config --libs sdl3)
    echo "[INFO] SDL3 detectado via pkg-config"
elif [ -d "/opt/homebrew/include/SDL3" ]; then
    SDL3_CFLAGS="-I/opt/homebrew/include"
    SDL3_LIBS="-L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib -lSDL3"
    echo "[INFO] SDL3 detectado em /opt/homebrew (Apple Silicon)"
elif [ -d "/usr/local/include/SDL3" ]; then
    SDL3_CFLAGS="-I/usr/local/include"
    SDL3_LIBS="-L/usr/local/lib -Wl,-rpath,/usr/local/lib -lSDL3"
    echo "[INFO] SDL3 detectado em /usr/local (Intel Mac)"
else
    echo "[AVISO] SDL3 nao encontrado via Homebrew/pkg-config. Tentando flags padrao..."
    SDL3_CFLAGS="-Ithird_party/SDL3/include"
    SDL3_LIBS="-lSDL3"
fi

# Flags comuns
COMMON_INCLUDES="-Iinclude -Ithird_party/rcheevos/include -Ithird_party/rcheevos/src -Ithird_party/libchdr/include -Ithird_party/libchdr -Ithird_party/SDL3/include -Ithird_party/Vulkan-Headers/include -Ithird_party/libretro-common/include $SDL3_CFLAGS"
COMMON_DEFS="-DRC_CLIENT_SUPPORTS_HASH -DZSTD_DISABLE_ASM -DGL_SILENCE_DEPRECATION"

# 3. Compilar libchdr (C)
echo "[1/4] Compilando libchdr..."
clang -O2 $COMMON_DEFS $COMMON_INCLUDES -c third_party/libchdr/unity.c -o build/macos_obj/chdr_unity.o

# 4. Compilar rcheevos (C)
echo "[2/4] Compilando rcheevos..."
RCHEEVOS_SRCS=(
    third_party/rcheevos/src/*.c
    third_party/rcheevos/src/rapi/*.c
    third_party/rcheevos/src/rcheevos/*.c
    third_party/rcheevos/src/rhash/*.c
)
for src in ${RCHEEVOS_SRCS[@]}; do
    [ -f "$src" ] || continue
    obj="build/macos_obj/rc_$(basename "$src" .c).o"
    clang -O2 $COMMON_DEFS $COMMON_INCLUDES -c "$src" -o "$obj"
done

# 5. Compilar Modulos Karamelo (C++20)
echo "[3/4] Compilando fontes C++ do Karamelo..."
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
    src/main_linux.cpp
)

for src in "${KARAMELO_SRCS[@]}"; do
    echo "  Compilando $src..."
    obj="build/macos_obj/$(basename "$src" .cpp).o"
    clang++ -std=c++20 -O2 $COMMON_DEFS $COMMON_INCLUDES -c "$src" -o "$obj"
done

# 6. Linkar Executavel Principal
echo "[4/4] Linkando app/Karamelo..."
clang++ -std=c++20 build/macos_obj/*.o \
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
    build/macos_obj/karamelo_math.o \
    build/macos_obj/netplay_protocol.o \
    build/macos_obj/archive_helper.o \
    build/macos_obj/updater.o \
    -lpthread -ldl \
    -o build/karamelo_tests

./build/karamelo_tests
