#!/bin/bash
# =====================================================================
#   Karamelo - Deploy All-in-One exclusivo para Linux
#   Uso: ./deploy_linux.sh
# =====================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "====================================================================="
echo "  Karamelo - Pipeline Linux: Build, Test, Package & Deploy"
echo "====================================================================="
echo ""

# 1. Carregar credenciais do servidor
if [ -f "deploy_env.sh" ]; then
    source "deploy_env.sh"
elif [ -f "deploy_env.bat" ]; then
    # Extrai variaveis do deploy_env.bat caso o usuario compartilhe o arquivo
    eval $(grep -E -i 'set (SERVER_IP|SERVER_USER|REMOTE_DIR)=' deploy_env.bat | sed -E 's/.*set ([^=]+)=(.*)/export \1="\2"/i' | tr -d '\r')
fi

SERVER_IP="${SERVER_IP:-187.127.59.127}"
SERVER_USER="${SERVER_USER:-root}"
REMOTE_DIR="${REMOTE_DIR:-/data/downloads/}"

# 0. So publica o que esta commitado e no GitHub (mesma regra do deploy_all.bat):
#    o binario que o jogador baixa tem que sair de um codigo que existe no repo.
#    dist/version.json fica de fora - o empacotamento regrava ele.
if ! git diff --quiet HEAD -- . ':(exclude)dist/version.json'; then
    echo "[ERRO] Ha alteracoes nao commitadas - deploy cancelado:"
    git status --short --untracked-files=no -- . ':(exclude)dist/version.json'
    exit 1
fi
git fetch -q origin || { echo "[ERRO] Nao consegui falar com o GitHub (git fetch)."; exit 1; }
if [ -z "$(git branch -r --contains HEAD)" ]; then
    echo "[ERRO] O commit atual ainda nao esta no GitHub - rode git push antes."
    exit 1
fi

# O version.json de partida e o do site: o bloco das outras plataformas so
# existe la (tools/release_manifest.py).
python3 tools/release_manifest.py fetch

# 2. Compilacao, testes e empacotamento da release Linux
echo "[1/2] Compilando, testando e empacotando release Linux..."
./package_linux.sh

APP_VER=$(grep '#define APP_VERSION' include/app_info.h | awk '{print $3}' | tr -d '"\r')
LOCAL_TAR="dist/Karamelo_v${APP_VER}_Linux64.tar.gz"
LOCAL_BIN="dist/Karamelo_linux"
LOCAL_JSON="dist/version.json"

if [ ! -f "$LOCAL_TAR" ]; then
    echo "[FALHA] Pacote $LOCAL_TAR nao foi encontrado."
    exit 1
fi

# Manifesto final: tira binario de plataforma nao refeito, notas do SITE_SYNC.
if ! python3 tools/release_manifest.py finalize --uploading linux_bin; then
    echo "[FALHA] version.json incompleto - deploy cancelado antes do upload."
    exit 1
fi

# 3. Upload via SCP
echo ""
echo "[2/2] Enviando arquivos para $SERVER_USER@$SERVER_IP:$REMOTE_DIR..."
if scp -o StrictHostKeyChecking=no "$LOCAL_TAR" "$LOCAL_BIN" "$LOCAL_JSON" data/ports.json "$SERVER_USER@$SERVER_IP:$REMOTE_DIR"; then
    scp -o StrictHostKeyChecking=no fix_server.sh "$SERVER_USER@$SERVER_IP:/root/fix_server.sh" 2>/dev/null || true
    echo ""
    echo "Sincronizando com containers do Coolify e ajustando permissoes..."
    ssh -o StrictHostKeyChecking=no "$SERVER_USER@$SERVER_IP" "sed -i 's/\r$//' /root/fix_server.sh && chmod +x /root/fix_server.sh && /root/fix_server.sh"
    echo ""
    echo "====================================================================="
    echo "  [DEPLOY LINUX CONCLUIDO COM SUCESSO - VERSAO v$APP_VER]"
    echo "====================================================================="
    echo ""
    echo "  Downloads disponiveis em:"
    echo "  - https://karamelo-emu.com/downloads/version.json"
    echo "  - https://karamelo-emu.com/downloads/Karamelo_linux"
    echo "  - https://karamelo-emu.com/downloads/Karamelo_v${APP_VER}_Linux64.tar.gz"
    echo "====================================================================="
else
    echo ""
    echo "====================================================================="
    echo "  [PACOTE LINUX PRONTO EM dist/] - Falha na conexao SSH"
    echo "====================================================================="
    echo "  Os arquivos foram gerados e validados:"
    echo "  - $LOCAL_TAR"
    echo "  - $LOCAL_BIN"
    echo "  - $LOCAL_JSON"
    echo ""
    echo "  O servidor de producao configurado e $SERVER_USER@$SERVER_IP."
    echo "====================================================================="
fi
