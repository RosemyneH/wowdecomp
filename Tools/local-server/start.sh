#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

usage() {
    echo "Usage: WOWDECOMP_SERVER_BACKEND=native|docker|auto $0" >&2
    echo "  native — your compiled authserver/worldserver (WOWDECOMP_ACORE_BIN)" >&2
    echo "  docker — acore-docker under test/local-server/ (needs Docker)" >&2
    echo "  auto   — native if binaries exist, else docker" >&2
    exit 1
}

backend="${WOWDECOMP_SERVER_BACKEND:-auto}"

case "$backend" in
    native)
        exec "$ROOT/Tools/local-server/start-native.sh" "$@"
        ;;
    docker)
        exec "$ROOT/Tools/local-server/start-docker.sh" "$@"
        ;;
    auto|acore)
        if "$ROOT/Tools/local-server/start-native.sh"; then
            exit 0
        fi
        echo "Native AzerothCore not found, trying Docker..." >&2
        if command -v docker >/dev/null 2>&1; then
            exec "$ROOT/Tools/local-server/start-docker.sh" "$@"
        fi
        echo "No native AzerothCore binaries found and Docker is not installed." >&2
        echo "Point wowdecomp at your build:" >&2
        echo "  export WOWDECOMP_ACORE_BIN=/path/to/acore/bin" >&2
        echo "  export WOWDECOMP_ACORE_CONFIG_DIR=/path/to/configs  # if not ../etc" >&2
        echo "  WOWDECOMP_SERVER_BACKEND=native $0" >&2
        usage
        ;;
    *)
        usage
        ;;
esac
