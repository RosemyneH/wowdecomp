#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SERVER_DIR="${WOWDECOMP_ACORE_DIR:-$ROOT/test/local-server/acore-docker}"

if ! command -v docker >/dev/null 2>&1; then
    echo "Docker backend selected but docker is not installed." >&2
    echo "Use the default TSWoW stack: $ROOT/Tools/local-server/start.sh" >&2
    exit 1
fi

if ! docker compose version >/dev/null 2>&1; then
    echo "docker compose plugin is required (docker-compose v2)." >&2
    exit 1
fi

mkdir -p "$(dirname "$SERVER_DIR")"

if [[ ! -d "$SERVER_DIR/.git" ]]; then
    git clone --depth 1 https://github.com/azerothcore/acore-docker.git "$SERVER_DIR"
fi

cd "$SERVER_DIR"

if [[ ! -f .env ]]; then
    cp -n env/dist/.env.docker .env 2>/dev/null || true
    cat >> .env <<'EOF'

DOCKER_AUTH_EXTERNAL_PORT=3724
DOCKER_WORLD_EXTERNAL_PORT=8085
DOCKER_DB_EXTERNAL_PORT=63306
EOF
fi

echo "Pulling images (first run can take a while)..."
docker compose pull
docker compose up -d

cat <<EOF

AzerothCore (Docker, gitignored): $SERVER_DIR
Auth 127.0.0.1:3724  World 127.0.0.1:8085
Stop: $ROOT/Tools/local-server/stop.sh  (with WOWDECOMP_SERVER_BACKEND=docker)
EOF
