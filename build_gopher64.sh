#!/bin/bash
# =====================================================================
#   Karamelo - compila o core de N64 (Gopher64) no Linux e no macOS
#
#   O mesmo papel do build_gopher64.bat: os outros cores vem do buildbot
#   do libretro pelo download_cores, mas este nao existe la. Ele e
#   compilado aqui, do fonte em third_party/gopher64, porque carrega
#   correcoes nossas.
#
#   Precisa de: Rust (rustup escolhe a versao do rust-toolchain.toml),
#   clang, cmake e os cabecalhos do Vulkan.
# =====================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

case "$(uname -s)" in
    Darwin) EXT=dylib ;;
    *)      EXT=so ;;
esac

if [ ! -f third_party/gopher64/Cargo.toml ]; then
    echo "[FALHA] third_party/gopher64 nao encontrado."
    exit 1
fi

echo "[GOPHER64] Compilando (release)... isso leva alguns minutos."
# Sem a interface propria do Gopher64 (Slint): o Karamelo e a interface, e ela
# so traria dependencias de sistema a mais, como o fontconfig.
(cd third_party/gopher64 && cargo build --release --lib --no-default-features)

TARGET_DIR="${CARGO_TARGET_DIR:-third_party/gopher64/target}"
SRC="$TARGET_DIR/release/libgopher64.$EXT"
if [ ! -f "$SRC" ]; then
    echo "[FALHA] $SRC nao foi gerada."
    exit 1
fi

mkdir -p cores app/cores
cp "$SRC" "cores/n64_gopher.$EXT"
cp "$SRC" "app/cores/n64_gopher.$EXT"
echo "[GOPHER64] Pronto: app/cores/n64_gopher.$EXT"
