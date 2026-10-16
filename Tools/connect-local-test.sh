#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REALM_HOST="${WOWDECOMP_REALM_HOST:-127.0.0.1}"

echo "==> wowdecomp play client → AzerothCore @ $REALM_HOST"
"$ROOT/Tools/setup-linux-test-client.sh" "$@"

echo ""
if command -v ss >/dev/null 2>&1; then
    if ss -ltn | grep -q ':3724 '; then
        echo "Auth port 3724 is open — run: cd $ROOT/test/play-335 && ./WowClient64"
    else
        echo "Start AzerothCore first: $ROOT/Tools/local-server/start.sh"
    fi
fi
