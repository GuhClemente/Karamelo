#!/bin/bash
# Publica /data/downloads no site karamelo-emu.com.
#
# /data/downloads e a fonte da verdade: o que estiver la aparece no site, o que
# for apagado de la some do site. Rode este script depois de qualquer upload.
#
# A versao anterior tinha tres defeitos que juntos derrubaram uma publicacao:
#
#   1. Copiava para TODOS os containers em execucao. Os zips de release foram
#      parar dentro do coolify-proxy, do coolify-sentinel e do buildkit.
#   2. So copiava, nunca apagava. Arquivos de versoes antigas ficavam servidos
#      para sempre, e apagar da origem nao adiantava nada.
#   3. Nao reiniciava o app. O Next.js em modo standalone monta a lista de
#      arquivos de public/ no boot, entao arquivo copiado com o servidor no ar
#      nao era servido: dava 404 mesmo estando no disco do container.
#
# Aqui: acha so o container do site pelo dominio nos labels, espelha (copia e
# poda), e reinicia o app no fim para o Next.js reconstruir a lista.

set -u

SRC=/data/downloads
DOMAIN=karamelo-emu.com

# Diretorios que o Next.js standalone serve como public/.
TARGET_DIRS="/app/public/downloads /app/.next/standalone/public/downloads"

# Nao publicar o proprio script de deploy no site.
SKIP="fix_server.sh"

echo "=== 1. Permissoes em $SRC ==="
chmod -R 755 "$SRC" 2>/dev/null || true

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
echo "=== 3. Espelhando $SRC nos containers ==="
for c in $APPS; do
    name=$(docker inspect -f '{{.Name}}' "$c" | sed 's|^/||')
    echo "  container: $name"

    for dir in $TARGET_DIRS; do
        docker exec "$c" mkdir -p "$dir" 2>/dev/null || continue

        # Poda: remove do destino o que nao existe mais na origem.
        docker exec "$c" sh -c "ls -1 $dir 2>/dev/null" | tr -d '\r' | while read -r f; do
            [ -n "$f" ] || continue
            if [ ! -e "$SRC/$f" ]; then
                echo "    - removendo obsoleto: $dir/$f"
                docker exec "$c" rm -f "$dir/$f" 2>/dev/null || true
            fi
        done

        # Copia tudo que esta na origem.
        for f in "$SRC"/*; do
            [ -f "$f" ] || continue
            base=$(basename "$f")
            [ "$base" = "$SKIP" ] && continue
            if docker cp "$f" "$c:$dir/$base" >/dev/null 2>&1; then
                echo "    + $dir/$base"
            fi
        done

        # O script nunca deve ficar publico no site.
        docker exec "$c" rm -f "$dir/$SKIP" 2>/dev/null || true
        docker exec "$c" chmod -R 755 "$dir" 2>/dev/null || true
    done
done

echo ""
echo "=== 4. Reiniciando o app para o Next.js reler public/ ==="
# Sem isto, arquivo novo continua dando 404 e arquivo apagado passa a dar 500,
# porque a lista de arquivos estaticos so e montada no boot do servidor.
for c in $APPS; do
    name=$(docker inspect -f '{{.Name}}' "$c" | sed 's|^/||')
    if docker restart "$c" >/dev/null 2>&1; then
        echo "  reiniciado: $name"
    else
        echo "  [AVISO] falha ao reiniciar $name - os downloads novos podem dar 404."
    fi
done

echo ""
echo "=== 5. Aguardando o app voltar ==="
for i in 1 2 3 4 5 6 7 8 9 10; do
    if curl -sf -o /dev/null --max-time 5 "https://$DOMAIN/downloads/version.json"; then
        echo "  no ar."
        break
    fi
    sleep 3
done

echo ""
echo "=== 6. Conferindo o que o site esta servindo ==="
for f in "$SRC"/*; do
    [ -f "$f" ] || continue
    base=$(basename "$f")
    [ "$base" = "$SKIP" ] && continue
    code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 20 "https://$DOMAIN/downloads/$base")
    printf '  %-4s %s\n' "$code" "$base"
done

echo ""
echo "=== PRONTO ==="
