#!/bin/bash
echo "=== 1. Localizando arquivos v0.9.0 no servidor ==="
TARGETS=$(find / -name "MiSTer_4_ALL_v0.9.0_Win64.zip" 2>/dev/null)
echo "Encontrado em:"
echo "$TARGETS"

echo ""
echo "=== 2. Copiando v0.9.1 para os mesmos locais ==="
for file in $TARGETS; do
    dir=$(dirname "$file")
    echo "Copiando para $dir..."
    cp -fv /data/downloads/MiSTer_4_ALL_v0.9.1_Win64.zip "$dir/"
    cp -fv /data/downloads/MiSTer_4_ALL.exe "$dir/"
    cp -fv /data/downloads/version.json "$dir/"
    chmod 755 "$dir/MiSTer_4_ALL_v0.9.1_Win64.zip"
    chmod 755 "$dir/MiSTer_4_ALL.exe"
    chmod 755 "$dir/version.json"
done

echo ""
echo "=== 3. Injetando diretamente nos containers Docker ==="
for c in $(docker ps -q); do
    cname=$(docker inspect -f '{{.Name}}' "$c")
    docker cp /data/downloads/MiSTer_4_ALL_v0.9.1_Win64.zip "$c:/app/public/downloads/" 2>/dev/null && echo " -> Injetado no container $cname (/app/public/downloads/)" || true
    docker cp /data/downloads/MiSTer_4_ALL.exe "$c:/app/public/downloads/" 2>/dev/null || true
    docker cp /data/downloads/version.json "$c:/app/public/downloads/" 2>/dev/null || true
done

chmod -R 755 /data/downloads/
echo ""
echo "=== 4. Teste de presenca do arquivo ==="
for file in $TARGETS; do
    dir=$(dirname "$file")
    ls -lh "$dir/MiSTer_4_ALL_v0.9.1_Win64.zip"
done

echo ""
echo "=== TUDO PRONTO! ==="