#!/bin/bash
set -e

# =====================================================================
#   Karamelo - Build Script para Linux / WSL (Ubuntu 24.04+)
# =====================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "====================================================================="
echo "  Karamelo - Linux Build Pipeline"
echo "====================================================================="

mkdir -p build/linux_obj
mkdir -p app

# 1. Obter Versao
APP_VER=$(grep "#define APP_VERSION " include/app_info.h | awk '{print $3}' | tr -d '"')
echo "[INFO] Versao do Karamelo: $APP_VER"

# 2. Compilar SDL3 Estatico se necessario
if [ ! -f "build/sdl3_linux/libSDL3.a" ]; then
    echo "[SDL3] Configurando e compilando libSDL3.a estatica..."
    cmake -S third_party/SDL3 -B build/sdl3_linux -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DSDL_STATIC=ON \
        -DSDL_SHARED=OFF \
        -DSDL_TEST_LIBRARY=OFF \
        -DSDL_EXAMPLES=OFF \
        -DSDL_DISABLE_INSTALL=ON \
        -DSDL_DISABLE_INSTALL_DOCS=ON
    cmake --build build/sdl3_linux --config Release -j$(nproc)
fi

echo "[INFO] SDL3 estatico pronto em build/sdl3_linux/libSDL3.a"

# Flags comuns
COMMON_INCLUDES="-Iinclude -Ithird_party/rcheevos/include -Ithird_party/rcheevos/src -Ithird_party/libchdr/include -Ithird_party/libchdr -Ithird_party/SDL3/include -Ithird_party/Vulkan-Headers/include -Ithird_party/libretro-common/include"
COMMON_DEFS="-D_GNU_SOURCE -DRC_CLIENT_SUPPORTS_HASH -DZSTD_DISABLE_ASM"

# 3. Compilar libchdr (C)
echo "[1/4] Compilando libchdr..."
gcc -O2 $COMMON_DEFS $COMMON_INCLUDES -c third_party/libchdr/unity.c -o build/linux_obj/chdr_unity.o

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
    obj="build/linux_obj/rc_$(basename "$src" .c).o"
    gcc -O2 $COMMON_DEFS $COMMON_INCLUDES -c "$src" -o "$obj"
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
    obj="build/linux_obj/$(basename "$src" .cpp).o"
    g++ -std=c++20 -O2 $COMMON_DEFS $COMMON_INCLUDES -c "$src" -o "$obj"
done

# 6. Linkar Executavel Principal
echo "[4/4] Linkando app/Karamelo..."
g++ -std=c++20 build/linux_obj/*.o build/sdl3_linux/libSDL3.a \
    -lpthread -ldl -lm -lGL \
    -o app/Karamelo

echo "====================================================================="
echo "  [SUCESSO] app/Karamelo construido com sucesso!"
cp -f app/Karamelo app/karamelo 2>/dev/null || true
ln -sf app/Karamelo karamelo 2>/dev/null || true
ls -lh app/Karamelo
echo "====================================================================="

# 7. Compilar e rodar a suite de testes unitarios
echo "[TESTES] Compilando e executando testes unitarios..."
g++ -std=c++20 -O2 $COMMON_DEFS $COMMON_INCLUDES \
    tests/test_main.cpp \
    tests/test_config.cpp \
    tests/test_archive.cpp \
    tests/test_netplay.cpp \
    tests/test_math_video.cpp \
    tests/test_updater.cpp \
    build/linux_obj/karamelo_math.o \
    build/linux_obj/netplay_protocol.o \
    build/linux_obj/archive_helper.o \
    build/linux_obj/updater.o \
    -lpthread -ldl \
    -o build/karamelo_tests

./build/karamelo_tests
