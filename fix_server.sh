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
echo "=== 2. Removendo pacotes de versoes anteriores ==="
# Cada release deixava o zip da anterior para tras, e o disco acumulava 300 MB
# por versao publicada. O que fica e so o pacote que o version.json anuncia -
# ninguem baixa uma versao antiga de proposito, e o auto-update nunca pede.
#
# A versao corrente vem do proprio version.json que acabou de subir, e nao de
# um parametro: se o manifesto e o zip discordarem, o site esta quebrado de
# qualquer jeito e apagar seria a menor das preocupacoes.
#
# Isto roda ANTES do restart de proposito. O Next.js standalone monta a lista
# de arquivos de public/ no boot, entao arquivo apagado com o app no ar passa a
# responder 500 ate alguem reiniciar. Apagando antes, o mesmo restart que
# publica o novo pacote ja esquece o antigo.
ATUAL=""
ATUAL_LINUX=""
if [ -f "$SRC/version.json" ]; then
    ATUAL=$(grep -o '"zip_url"[^,]*' "$SRC/version.json" | sed 's|.*/||; s|"||g')
    ATUAL_LINUX=$(grep -o '"linux_tar_url"[^,]*' "$SRC/version.json" | sed 's|.*/||; s|"||g')
fi

if [ -z "$ATUAL" ] || [ ! -f "$SRC/$ATUAL" ]; then
    echo "  [AVISO] nao consegui identificar o pacote Win64 atual pelo version.json."
    echo "  Nada foi apagado do Windows - conferir a mao e melhor que apagar por engano."
else
    echo "  mantendo Win64: $ATUAL"
    apagados=0
    for z in "$SRC"/Karamelo_v*_Win64.zip; do
        [ -f "$z" ] || continue
        base=$(basename "$z")
        [ "$base" = "$ATUAL" ] && continue
        tam=$(du -h "$z" | cut -f1)
        if rm -f "$z"; then
            echo "  apagado:  $base ($tam)"
            apagados=$((apagados + 1))
        else
            echo "  [ERRO] nao consegui apagar $base"
        fi
    done
    [ "$apagados" = "0" ] && echo "  nenhuma versao Win64 antiga para apagar."
fi

if [ -n "$ATUAL_LINUX" ] && [ -f "$SRC/$ATUAL_LINUX" ]; then
    echo "  mantendo Linux64: $ATUAL_LINUX"
    apagados_linux=0
    for z in "$SRC"/Karamelo_v*_Linux64.tar.gz "$SRC"/Karamelo_v*_Linux64.zip; do
        [ -f "$z" ] || continue
        base=$(basename "$z")
        [ "$base" = "$ATUAL_LINUX" ] && continue
        tam=$(du -h "$z" | cut -f1)
        if rm -f "$z"; then
            echo "  apagado Linux:  $base ($tam)"
            apagados_linux=$((apagados_linux + 1))
        else
            echo "  [ERRO] nao consegui apagar $base"
        fi
    done
    [ "$apagados_linux" = "0" ] && echo "  nenhuma versao Linux64 antiga para apagar."
fi

echo ""
echo "=== 3. Procurando o container do site ($DOMAIN) ==="
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
echo "=== 4. Reiniciando o app para o Next.js reler public/ ==="
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
echo "=== 5. Aguardando o app voltar ==="
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
echo "=== 6. Conferindo o que o site esta servindo ==="
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
