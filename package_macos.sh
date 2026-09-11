#!/bin/bash
# =====================================================================
#   Karamelo - macOS Release Packaging Pipeline (Apple Silicon arm64)
# =====================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "======================================================="
echo "  Karamelo - macOS Packaging & Release Generator"
echo "======================================================="
echo ""

# 1. Obter versao do app_info.h
APP_VER=$(grep '#define APP_VERSION' include/app_info.h | awk '{print $3}' | tr -d '"\r')
if [ -z "$APP_VER" ]; then
    echo "[ERRO] Nao foi possivel extrair APP_VERSION de include/app_info.h"
    exit 1
fi

DIST_NAME="Karamelo_v${APP_VER}_macOS_arm64"
DIST_DIR="$SCRIPT_DIR/dist/$DIST_NAME"

echo "[INFO] Versao detectada: v$APP_VER"
echo "[INFO] Destino do pacote: dist/$DIST_NAME"
echo ""

# 2. Compilar binario de producao
echo "[1/5] Compilando executavel nativo macOS (arm64)..."
./compile_macos.sh
if [ ! -f "app/Karamelo" ]; then
    echo "[ERRO] Binario app/Karamelo nao encontrado apos compilacao."
    exit 1
fi

# 3. Garantir que os cores macOS existem
echo ""
echo "[2/5] Verificando cores libretro macOS (*.dylib)..."
missing_cores=0
for check_core in cores/genesis.dylib cores/snes.dylib cores/nes.dylib cores/n64.dylib cores/psx.dylib; do
    if [ ! -f "$check_core" ] && [ ! -f "app/$check_core" ]; then
        missing_cores=1
        break
    fi
done

if [ "$missing_cores" -eq 1 ]; then
    echo "  Alguns cores estao faltando. Executando download_cores_macos.sh..."
    ./download_cores_macos.sh
else
    echo "  Cores basicos verificados."
fi

# 4. Estruturar o diretorio de distribuicao
echo ""
echo "[3/5] Estruturando pacote em dist/$DIST_NAME..."
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"
mkdir -p "$DIST_DIR/cores"
mkdir -p "$DIST_DIR/Wallpapers"
mkdir -p "$DIST_DIR/saves"
mkdir -p "$DIST_DIR/screenshots"
mkdir -p "$DIST_DIR/Config"
mkdir -p "$DIST_DIR/ports"
mkdir -p "$DIST_DIR/bios"
mkdir -p "$DIST_DIR/roms"

# Copiar executavel principal e definir permissao de execucao
cp -a "app/Karamelo" "$DIST_DIR/Karamelo"
chmod 0755 "$DIST_DIR/Karamelo"

# Criar script wrapper run.sh
cat << 'EOF' > "$DIST_DIR/run.sh"
#!/bin/sh
SCRIPT_PATH="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_PATH" || exit 1
exec ./Karamelo "$@"
EOF
chmod 0755 "$DIST_DIR/run.sh"

# Copiar cores macOS (.dylib)
if ls app/cores/*.dylib 1>/dev/null 2>&1; then
    cp -a app/cores/*.dylib "$DIST_DIR/cores/"
elif ls cores/*.dylib 1>/dev/null 2>&1; then
    cp -a cores/*.dylib "$DIST_DIR/cores/"
fi

# Remover atributos de quarentena
xattr -rd com.apple.quarantine "$DIST_DIR" 2>/dev/null || true

# Copiar Wallpapers (.raw)
if ls app/Wallpapers/*.raw 1>/dev/null 2>&1; then
    cp -a app/Wallpapers/*.raw "$DIST_DIR/Wallpapers/"
elif ls Wallpapers/*.raw 1>/dev/null 2>&1; then
    cp -a Wallpapers/*.raw "$DIST_DIR/Wallpapers/"
fi

# Copiar guias de BIOS
if [ -f "packaging/bios-guide/BIOS_NECESSARIOS.txt" ]; then
    cp -a "packaging/bios-guide/BIOS_NECESSARIOS.txt" "$DIST_DIR/bios/"
fi
if [ -f "docs/GUIA_COMPLETO_BIOS.md" ]; then
    cp -a "docs/GUIA_COMPLETO_BIOS.md" "$DIST_DIR/bios/GUIA_COMPLETO_BIOS.txt"
fi

# Criar pastas de ROMs dos 35 sistemas com seus LEIA-ME.txt
if [ -d "packaging/rom-folder-guides" ]; then
    for sdir in packaging/rom-folder-guides/*; do
        if [ -d "$sdir" ]; then
            sname=$(basename "$sdir")
            mkdir -p "$DIST_DIR/roms/$sname"
            if [ -f "$sdir/LEIA-ME.txt" ]; then
                cp -a "$sdir/LEIA-ME.txt" "$DIST_DIR/roms/$sname/"
            fi
        fi
    done
fi

# Copiar guias e licencas
if [ -f "packaging/ports-guide/LEIA-ME.txt" ]; then
    cp -a "packaging/ports-guide/LEIA-ME.txt" "$DIST_DIR/ports/LEIA-ME.txt"
fi
if [ -f "packaging/release-guide/LEIAME.txt" ]; then
    cp -a "packaging/release-guide/LEIAME.txt" "$DIST_DIR/LEIAME.txt"
fi
if [ -f "LICENSE.md" ]; then
    cp -a "LICENSE.md" "$DIST_DIR/LICENSE.md"
fi
if [ -f "THIRD-PARTY-NOTICES.md" ]; then
    cp -a "THIRD-PARTY-NOTICES.md" "$DIST_DIR/THIRD-PARTY-NOTICES.md"
fi

# Seguranca: garantir que nenhum dump acidental de ROM ou BIOS protegida por copyright entre no pacote
find "$DIST_DIR/roms" -type f ! -name "LEIA-ME.txt" -delete 2>/dev/null || true
find "$DIST_DIR/cores" -type f \( -name "*.bin" -o -name "*.iso" \) -delete 2>/dev/null || true

# 5. Compactar arquivos de release
echo ""
echo "[4/5] Gerando arquivos compactados em dist/..."
mkdir -p dist
cd dist

TAR_FILE="${DIST_NAME}.tar.gz"
echo "  Compactando tar.gz: $TAR_FILE..."
tar -czf "$TAR_FILE" "$DIST_NAME"

# Se zip estiver disponivel, gera tambem .zip
if command -v zip >/dev/null 2>&1; then
    ZIP_FILE="${DIST_NAME}.zip"
    echo "  Compactando zip: $ZIP_FILE..."
    zip -r -q "$ZIP_FILE" "$DIST_NAME"
fi

cd "$SCRIPT_DIR"

# 6. Preparar executavel avulso para Auto-Updater e atualizar version.json
echo ""
echo "[5/5] Atualizando dist/Karamelo_mac e dist/version.json..."
cp -a "app/Karamelo" "dist/Karamelo_mac"
chmod 0755 "dist/Karamelo_mac"

python3 - << PYEOF
import json
import hashlib
import os
from datetime import datetime

ver = "$APP_VER"
json_path = "dist/version.json"
mac_bin = "dist/Karamelo_mac"
mac_tar = f"dist/Karamelo_v{ver}_macOS_arm64.tar.gz"

data = {}
if os.path.exists(json_path):
    try:
        with open(json_path, "r", encoding="utf-8-sig") as f:
            data = json.load(f)
    except Exception as e:
        print(f"  [AVISO] Erro lendo version.json existente: {e}")

# Calcula hash e tamanho do binario macOS
bin_size = os.path.getsize(mac_bin) if os.path.exists(mac_bin) else 0
bin_sha = ""
if os.path.exists(mac_bin):
    h = hashlib.sha256()
    with open(mac_bin, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    bin_sha = h.hexdigest().upper()

# Atualiza campos genericos se nao existirem
if "version" not in data:
    data["version"] = ver
if "title" not in data:
    data["title"] = f"Karamelo v{ver}"
if "release_date" not in data:
    data["release_date"] = datetime.now().strftime("%Y-%m-%d")
if "notes" not in data:
    data["notes"] = f"Lancamento oficial do Karamelo com 35 sistemas nativos e Auto-Update."
if "force_full_package" not in data:
    data["force_full_package"] = False

# Campos do macOS
data["macos_bin_url"] = "https://karamelo-emu.com/downloads/Karamelo_mac"
data["macos_bin_size"] = bin_size
data["macos_bin_sha256"] = bin_sha
data["macos_tar_url"] = f"https://karamelo-emu.com/downloads/Karamelo_v{ver}_macOS_arm64.tar.gz"

with open(json_path, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=4, ensure_ascii=False)

print(f"  version.json atualizado com sucesso!")
print(f"  macOS Bin: {bin_size} bytes (SHA256: {bin_sha})")
print(f"  macOS Tar: {mac_tar}")
PYEOF

echo ""
echo "======================================================="
echo "  PACOTE MACOS GERADO COM SUCESSO"
echo "======================================================="
echo "  Pacote: dist/$TAR_FILE"
echo "  Updater: dist/Karamelo_mac"
echo "  Manifesto: dist/version.json"
echo "======================================================="
