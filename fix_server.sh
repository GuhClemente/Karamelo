#!/bin/bash
echo "=== 1. Localizando arquivos v0.9.0 no servidor ==="
TARGETS=$(find / -name "MiSTer_4_ALL_v0.9.0_Win64.zip" 2>/dev/null)
echo "Encontrado em:"
echo "$TARGETS"

echo ""
echo "=== 2. Copiando v0.9.1 para os mesmos locais no host ==="
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
echo "=== 3. Injetando diretamente nos containers Docker do Coolify ==="
for c in $(docker ps -q); do
    cname=$(docker inspect -f '{{.Name}}' "$c")
    echo "Container: $cname"
    docker exec "$c" mkdir -p /app/public/downloads 2>/dev/null || true
    docker exec "$c" mkdir -p /app/.next/standalone/public/downloads 2>/dev/null || true
    
    docker cp /data/downloads/MiSTer_4_ALL_v0.9.1_Win64.zip "$c:/app/public/downloads/" 2>/dev/null && echo " -> Copiado para $cname:/app/public/downloads/" || true
    docker cp /data/downloads/MiSTer_4_ALL.exe "$c:/app/public/downloads/" 2>/dev/null || true
    docker cp /data/downloads/version.json "$c:/app/public/downloads/" 2>/dev/null || true
    
    docker cp /data/downloads/MiSTer_4_ALL_v0.9.1_Win64.zip "$c:/app/.next/standalone/public/downloads/" 2>/dev/null || true
    docker cp /data/downloads/MiSTer_4_ALL.exe "$c:/app/.next/standalone/public/downloads/" 2>/dev/null || true
    docker cp /data/downloads/version.json "$c:/app/.next/standalone/public/downloads/" 2>/dev/null || true
    
    docker exec "$c" chmod -R 755 /app/public/downloads 2>/dev/null || true
    docker exec "$c" chmod -R 755 /app/.next/standalone/public/downloads 2>/dev/null || true
done

chmod -R 755 /data/downloads/
echo ""
echo "=== 4. Teste de presenca dos arquivos ==="
ls -lh /data/downloads/MiSTer_4_ALL_v0.9.1_Win64.zip

echo ""
echo "=== TUDO PRONTO! ==="