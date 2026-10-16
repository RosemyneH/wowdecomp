#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${WOWDECOMP_ACORE_BIN:-$ROOT/test/local-server/azerothcore-wotlk/env/dist/bin}"
CONFIG="${WOWDECOMP_ACORE_CONFIG_DIR:-$(dirname "$BIN")/etc}"

if [[ ! -x "$BIN/worldserver" ]]; then
    echo "Missing worldserver. Run: $ROOT/Tools/local-server/install-acore.sh" >&2
    exit 1
fi

"$ROOT/Tools/local-server/stop.sh" --world-only

for _ in 1 2 3 4 5; do
    ss -ltn | grep -q ':8085 ' || break
    sleep 1
done
if ss -ltn | grep -q ':8085 '; then
    echo "Port 8085 still in use after stop." >&2
    ss -ltnp | grep 8085 >&2 || true
    exit 1
fi

export WOWDECOMP_SERVER_BACKEND=native
WOWDECOMP_ACORE_QUIET=1 "$ROOT/Tools/local-server/start-native.sh" --auth-only

for _ in $(seq 1 20); do
    ss -ltn | grep -q ':3724 ' && break
    sleep 1
done
if ! ss -ltn | grep -q ':3724 '; then
    echo "Auth is not listening on 3724. Check:" >&2
    echo "  $ROOT/test/local-server/acore-run/logs/authserver.log" >&2
    echo "  $ROOT/Tools/local-server/bootstrap-acore-db.sh  # fixes realmlist flag" >&2
    exit 1
fi

echo ""
echo "Worldserver in this terminal — AC> prompt below."
echo "  account create wowdecomp wowdecomp"
echo "Ctrl+C stops world; auth keeps running on :3724"
cd "$BIN"
exec ./worldserver -c "$CONFIG/worldserver.conf"
