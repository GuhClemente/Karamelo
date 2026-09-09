#!/bin/bash
# Publica no site karamelo-emu.com o que esta em /data/downloads.
#
# Este arquivo mora em /root/ no servidor, de proposito: /data/downloads e
# servido publicamente, entao um script de deploy la dentro fica exposto em
# https://karamelo-emu.com/downloads/fix_server.sh - foi o que aconteceu.
#
# Como funciona de verdade:
#
# A aplicacao no Coolify ja tem um bind mount de /data/downloads para
# /app/public/downloads. O arquivo colocado em /data/downloads ja esta dentro
# do container no mesmo instante - nao existe nada para copiar. Copiar,
# alias, foi o que a primeira versao deste script fazia, para TODOS os
# containers em execucao, enchendo o coolify-proxy e o buildkit com centenas
# de MB de release.
#
# O que falta e so uma coisa: o Next.js em modo standalone monta a lista de
# arquivos de public/ no boot. Arquivo que aparece com o servidor no ar nao e
# servido (404) e arquivo que some passa a dar 500 - ambos mesmo com o disco
# correto. Por isso publicar = reiniciar o app.

set -u

SRC=/data/downloads
DOMAIN=karamelo-emu.com

echo "=== 1. Conteudo de $SRC ==="
chmod -R 755 "$SRC" 2>/dev/null || true
ls -1 "$SRC"

echo ""
echo "=== 2. Procurando o container do site ($DOMAIN) ==="
APPS=""
for c in $(docker ps -q); do
    name=$(docker inspect -f '{{.Name}}' "$c" | sed 's|^/||')
    case "$name" in
        *proxy*|coolify|coolify-db|coolify-redis|coolify-realtime|coolify-sentinel|buildx_*)
            continue ;;
    esac
    if docker inspect "$c" | grep -qi "$DOMAIN"; then
        APPS="$APPS $c"
        echo "  encontrado: $name"
    fi
done

if [ -z "$APPS" ]; then
    echo ""
    echo "  [ERRO] Nenhum container em execucao tem $DOMAIN nos labels."
    echo "  Nada foi publicado. Confira o dominio da aplicacao no Coolify."
    exit 1
fi

echo ""
echo "=== 3. Reiniciando o app para o Next.js reler public/ ==="
for c in $APPS; do
    name=$(docker inspect -f '{{.Name}}' "$c" | sed 's|^/||')
    if docker restart "$c" >/dev/null 2>&1; then
        echo "  reiniciado: $name"
    else
        echo "  [ERRO] falha ao reiniciar $name."
        exit 1
    fi
done

echo ""
echo "=== 4. Aguardando o app voltar ==="
up=0
for i in $(seq 1 20); do
    if curl -sf -o /dev/null --max-time 5 "https://$DOMAIN/downloads/version.json"; then
        echo "  no ar."
        up=1
        break
    fi
    sleep 3
done
[ "$up" = "1" ] || echo "  [AVISO] o app nao respondeu a tempo; conferindo mesmo assim."

echo ""
echo "=== 5. Conferindo o que o site esta servindo ==="
fail=0
for f in "$SRC"/*; do
    [ -f "$f" ] || continue
    base=$(basename "$f")
    code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 30 "https://$DOMAIN/downloads/$base")
    printf '  %-4s %s\n' "$code" "$base"
    [ "$code" = "200" ] || fail=1
done

echo ""
if [ "$fail" = "0" ]; then
    echo "=== PUBLICADO ==="
else
    echo "=== [FALHA] algum arquivo nao esta sendo servido - veja os codigos acima ==="
    exit 1
fi
