#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RUN_DIR="${WOWDECOMP_ACORE_RUN_DIR:-$ROOT/test/local-server/acore-run}"
PID_DIR="$RUN_DIR/pids"

stop_world_only=0
for arg in "$@"; do
    [[ "$arg" == "--world-only" ]] && stop_world_only=1
done

if [[ "${WOWDECOMP_SERVER_BACKEND:-}" == "docker" ]]; then
    SERVER_DIR="${WOWDECOMP_ACORE_DIR:-$ROOT/test/local-server/acore-docker}"
    if [[ -f "$SERVER_DIR/docker-compose.yml" ]]; then
        if docker info >/dev/null 2>&1; then
            docker compose -f "$SERVER_DIR/docker-compose.yml" down
        else
            sudo docker compose -f "$SERVER_DIR/docker-compose.yml" down
        fi
    fi
    exit 0
fi

rm -f "$PID_DIR/worldserver.pid"
if [[ "$stop_world_only" -eq 0 ]]; then
    rm -f "$PID_DIR/authserver.pid"
fi

if [[ "$stop_world_only" -eq 1 ]]; then
    "$ROOT/Tools/local-server/acore-kill.sh" world
else
    "$ROOT/Tools/local-server/acore-kill.sh" all
fi
