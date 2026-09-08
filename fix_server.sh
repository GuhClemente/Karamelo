#!/bin/bash
echo "=== 1. Ajustando permissoes na pasta /data/downloads ==="
chmod -R 755 /data/downloads 2>/dev/null || true

echo ""
echo "=== 2. Sincronizando arquivos com diretorios publicos no host ==="
TARGETS=$(find / -type d -name "downloads" 2>/dev/null | grep -E "public/downloads" || true)
for dir in $TARGETS; do
    echo "Sincronizando para host: $dir..."
    cp -afv /data/downloads/* "$dir/" 2>/dev/null || true
    chmod -R 755 "$dir" 2>/dev/null || true
done

echo ""
echo "=== 3. Injetando diretamente nos containers Docker do Coolify ==="
for c in $(docker ps -q); do
    cname=$(docker inspect -f '{{.Name}}' "$c")
    echo "Container: $cname"
    docker exec "$c" mkdir -p /app/public/downloads 2>/dev/null || true
    docker exec "$c" mkdir -p /app/.next/standalone/public/downloads 2>/dev/null || true
    
    for f in /data/downloads/*; do
        [ -f "$f" ] || continue
        fname=$(basename "$f")
        docker cp "$f" "$c:/app/public/downloads/$fname" 2>/dev/null && echo " -> /app/public/downloads/$fname" || true
        docker cp "$f" "$c:/app/.next/standalone/public/downloads/$fname" 2>/dev/null || true
    done
    
    docker exec "$c" chmod -R 755 /app/public/downloads 2>/dev/null || true
    docker exec "$c" chmod -R 755 /app/.next/standalone/public/downloads 2>/dev/null || true
done

chmod -R 755 /data/downloads 2>/dev/null || true
echo ""
echo "=== 4. Arquivos ativos em /data/downloads ==="
ls -lh /data/downloads/

echo ""
echo "=== TUDO PRONTO E SINCRONIZADO! ==="