#!/bin/bash
set -e

# =====================================================================
#   Karamelo - Download de Cores Nativos para Linux (.so)
# =====================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

mkdir -p app/cores
mkdir -p cores
mkdir -p /tmp/karamelo_cores_dl

BASE_URL="https://buildbot.libretro.com/nightly/linux/x86_64/latest"

declare -A CORES_MAP=(
    ["arcade_fbneo.so"]="fbneo_libretro.so.zip"
    ["neogeo.so"]="fbneo_libretro.so.zip"
    ["mame2003.so"]="mame2003_plus_libretro.so.zip"
    ["mame2010.so"]="mame2010_libretro.so.zip"
    ["nes.so"]="fceumm_libretro.so.zip"
    ["snes.so"]="snes9x_libretro.so.zip"
    ["genesis.so"]="genesis_plus_gx_libretro.so.zip"
    ["32x.so"]="picodrive_libretro.so.zip"
    ["gb.so"]="gambatte_libretro.so.zip"
    ["gba.so"]="mgba_libretro.so.zip"
    ["n64.so"]="mupen64plus_next_libretro.so.zip"
    ["n64_mupen.so"]="mupen64plus_next_libretro.so.zip"
    ["n64_parallel.so"]="parallel_n64_libretro.so.zip"
    ["psx.so"]="mednafen_psx_hw_libretro.so.zip"
    ["saturn.so"]="mednafen_saturn_libretro.so.zip"
    ["dreamcast.so"]="flycast_libretro.so.zip"
    ["psp.so"]="ppsspp_libretro.so.zip"
    ["atari2600.so"]="stella_libretro.so.zip"
    ["atari5200.so"]="a5200_libretro.so.zip"
    ["atari7800.so"]="prosystem_libretro.so.zip"
    ["sms.so"]="gearsystem_libretro.so.zip"
    ["pce.so"]="mednafen_pce_fast_libretro.so.zip"
    ["pcfx.so"]="mednafen_pcfx_libretro.so.zip"
    ["3do.so"]="opera_libretro.so.zip"
    ["nds.so"]="melonds_libretro.so.zip"
    ["amiga.so"]="puae_libretro.so.zip"
    ["c64.so"]="vice_x64_libretro.so.zip"
    ["spectrum.so"]="fuse_libretro.so.zip"
    ["coleco.so"]="gearcoleco_libretro.so.zip"
    ["ngp.so"]="mednafen_ngp_libretro.so.zip"
    ["wswan.so"]="mednafen_wswan_libretro.so.zip"
    ["lynx.so"]="handy_libretro.so.zip"
    ["jaguar.so"]="virtualjaguar_libretro.so.zip"
    ["msx.so"]="fmsx_libretro.so.zip"
    ["dosbox_pure.so"]="dosbox_pure_libretro.so.zip"
)

echo "====================================================================="
echo "  Baixando cores nativos Linux para Karamelo (.so)"
echo "====================================================================="

for target in "${!CORES_MAP[@]}"; do
    zip_name="${CORES_MAP[$target]}"
    if [ -f "cores/$target" ] && [ -f "app/cores/$target" ]; then
        echo "  [OK] $target ja instalado."
        continue
    fi

    echo "  -> Baixando $target ($zip_name)..."
    cd /tmp/karamelo_cores_dl
    if curl -s -f -O "$BASE_URL/$zip_name"; then
        unzip -q -o "$zip_name"
        extracted_so=$(unzip -l "$zip_name" | awk '{print $4}' | grep '\.so$' | head -n 1)
        if [ -n "$extracted_so" ] && [ -f "$extracted_so" ]; then
            cp "$extracted_so" "$SCRIPT_DIR/cores/$target"
            cp "$extracted_so" "$SCRIPT_DIR/app/cores/$target"
            echo "     [SUCESSO] $target instalado ($(stat -c%s "$extracted_so") bytes)"
        fi
        rm -f "$zip_name"
    else
        echo "     [AVISO] Nao foi possivel baixar $zip_name de $BASE_URL"
    fi
    cd "$SCRIPT_DIR"
done

echo "====================================================================="
echo "  Cores Linux configurados com sucesso!"
echo "====================================================================="
