#!/bin/bash
# deploy.sh - runs on the server, next to server.mjs and Dockerfile. Puts the chat
# behind the Coolify proxy as a container of its own; touches nothing else.
#
#   ./deploy.sh [tls]
#
# Without "tls" the route answers on plain http, which needs no DNS and lets the
# service be checked from inside the server. Use "tls" once the DNS record points
# here: Let's Encrypt limits failed attempts, so it must not be tried before.
#
# Secrets are made here on the first run and stay in $BASE/.env: reusing them is what
# keeps tags, logins and bans valid across deploys. See docs/CHAT_SERVER.md.
set -euo pipefail
BASE=/data/karamelo-chat
DOMAIN=chat.karamelo-emu.com
NAME=karamelo-chat
MODE="${1:-http}"
cd "$(dirname "$0")"

mkdir -p "$BASE/data"
chown 1000:1000 "$BASE/data"
if [ ! -f "$BASE/.env" ]; then
    umask 077
    {
        echo "CHAT_HASH_SECRET=$(openssl rand -hex 32)"
        echo "CHAT_ADMIN_TOKEN=$(openssl rand -hex 32)"
    } > "$BASE/.env"
fi
CLIENT_IDS="$(tr -d '\r\n' < "$BASE/client_ids")"
[ -n "$CLIENT_IDS" ] || { echo "no client id in $BASE/client_ids"; exit 1; }

docker build -q -t "$NAME:latest" . >/dev/null

LABELS=(
    --label traefik.enable=true
    --label traefik.docker.network=coolify
    --label "traefik.http.services.$NAME.loadbalancer.server.port=8787"
)
if [ "$MODE" = tls ]; then
    LABELS+=(
        --label "traefik.http.routers.$NAME.rule=Host(\`$DOMAIN\`)"
        --label "traefik.http.routers.$NAME.entrypoints=https"
        --label "traefik.http.routers.$NAME.tls=true"
        --label "traefik.http.routers.$NAME.tls.certresolver=letsencrypt"
        --label "traefik.http.routers.$NAME.service=$NAME"
        --label "traefik.http.routers.$NAME-http.rule=Host(\`$DOMAIN\`)"
        --label "traefik.http.routers.$NAME-http.entrypoints=http"
        --label "traefik.http.routers.$NAME-http.middlewares=$NAME-https"
        --label "traefik.http.routers.$NAME-http.service=$NAME"
        --label "traefik.http.middlewares.$NAME-https.redirectscheme.scheme=https"
    )
else
    LABELS+=(
        --label "traefik.http.routers.$NAME.rule=Host(\`$DOMAIN\`)"
        --label "traefik.http.routers.$NAME.entrypoints=http"
        --label "traefik.http.routers.$NAME.service=$NAME"
    )
fi

docker rm -f "$NAME" >/dev/null 2>&1 || true
docker run -d --name "$NAME" --restart unless-stopped --network coolify \
    --memory 256m --read-only --tmpfs /tmp \
    --env-file "$BASE/.env" -e CHAT_GOOGLE_CLIENT_IDS="$CLIENT_IDS" \
    -v "$BASE/data:/data" "${LABELS[@]}" "$NAME:latest" >/dev/null
echo "deployed ($MODE)"
