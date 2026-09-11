#!/bin/bash
set -e

# =====================================================================
#   Karamelo - Download de Cores Nativos para macOS (.dylib)
# =====================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

mkdir -p app/cores
mkdir -p cores
mkdir -p /tmp/karamelo_cores_macos_dl

BASE_URL="https://buildbot.libretro.com/nightly/apple/osx/arm64/latest"

CORES_LIST=(
    "arcade_fbneo.dylib:fbneo_libretro.dylib.zip"
    "neogeo.dylib:fbneo_libretro.dylib.zip"
    "mame2003.dylib:mame2003_plus_libretro.dylib.zip"
    "mame2010.dylib:mame2010_libretro.dylib.zip"
    "nes.dylib:fceumm_libretro.dylib.zip"
    "snes.dylib:snes9x_libretro.dylib.zip"
    "genesis.dylib:genesis_plus_gx_libretro.dylib.zip"
    "32x.dylib:picodrive_libretro.dylib.zip"
    "gb.dylib:gambatte_libretro.dylib.zip"
    "gba.dylib:mgba_libretro.dylib.zip"
    "n64.dylib:mupen64plus_next_libretro.dylib.zip"
    "n64_mupen.dylib:mupen64plus_next_libretro.dylib.zip"
    "n64_parallel.dylib:parallel_n64_libretro.dylib.zip"
    "psx.dylib:mednafen_psx_libretro.dylib.zip"
    "saturn.dylib:mednafen_saturn_libretro.dylib.zip"
    "dreamcast.dylib:flycast_libretro.dylib.zip"
    "psp.dylib:ppsspp_libretro.dylib.zip"
    "gamecube.dylib:dolphin_libretro.dylib.zip"
    "3ds.dylib:citra_libretro.dylib.zip"
    "neocd_alt.dylib:neocd_libretro.dylib.zip"
    "neocd.dylib:neocd_libretro.dylib.zip"
    "atari2600.dylib:stella_libretro.dylib.zip"
    "atari5200.dylib:a5200_libretro.dylib.zip"
    "atari7800.dylib:prosystem_libretro.dylib.zip"
    "sms.dylib:gearsystem_libretro.dylib.zip"
    "pce.dylib:mednafen_pce_fast_libretro.dylib.zip"
    "pcfx.dylib:mednafen_pcfx_libretro.dylib.zip"
    "3do.dylib:opera_libretro.dylib.zip"
    "nds.dylib:melonds_libretro.dylib.zip"
    "amiga.dylib:puae_libretro.dylib.zip"
    "c64.dylib:vice_x64_libretro.dylib.zip"
    "spectrum.dylib:fuse_libretro.dylib.zip"
    "coleco.dylib:gearcoleco_libretro.dylib.zip"
    "ngp.dylib:mednafen_ngp_libretro.dylib.zip"
    "wswan.dylib:mednafen_wswan_libretro.dylib.zip"
    "lynx.dylib:handy_libretro.dylib.zip"
    "jaguar.dylib:virtualjaguar_libretro.dylib.zip"
    "msx.dylib:fmsx_libretro.dylib.zip"
    "dosbox_pure.dylib:dosbox_pure_libretro.dylib.zip"
)

echo "====================================================================="
echo "  Baixando cores nativos macOS (ARM64) para Karamelo (.dylib)"
echo "====================================================================="

for item in "${CORES_LIST[@]}"; do
    target="${item%%:*}"
    zip_name="${item##*:}"

    if [ -f "cores/$target" ] && [ -f "app/cores/$target" ]; then
        echo "  [OK] $target ja instalado."
        continue
    fi

    echo "  -> Baixando $target ($zip_name)..."
    cd /tmp/karamelo_cores_macos_dl
    if curl -s -f -O "$BASE_URL/$zip_name"; then
        unzip -q -o "$zip_name"
        extracted_dylib=$(unzip -l "$zip_name" | awk '{print $4}' | grep '\.dylib$' | head -n 1)
        if [ -n "$extracted_dylib" ] && [ -f "$extracted_dylib" ]; then
            cp "$extracted_dylib" "$SCRIPT_DIR/cores/$target"
            cp "$extracted_dylib" "$SCRIPT_DIR/app/cores/$target"
            xattr -d com.apple.quarantine "$SCRIPT_DIR/cores/$target" 2>/dev/null || true
            xattr -d com.apple.quarantine "$SCRIPT_DIR/app/cores/$target" 2>/dev/null || true
            echo "     [SUCESSO] $target instalado"
        fi
        rm -f "$zip_name"
    else
        echo "     [AVISO] Nao foi possivel baixar $zip_name de $BASE_URL"
    fi
    cd "$SCRIPT_DIR"
done

echo "====================================================================="
echo "  Cores macOS configurados com sucesso!"
echo "====================================================================="
