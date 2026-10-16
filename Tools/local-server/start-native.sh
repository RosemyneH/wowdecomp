#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RUN_DIR="${WOWDECOMP_ACORE_RUN_DIR:-$ROOT/test/local-server/acore-run}"
PID_DIR="$RUN_DIR/pids"
LOG_DIR="$RUN_DIR/logs"

resolve_acore_bin() {
    if [[ -n "${WOWDECOMP_ACORE_BIN:-}" ]]; then
        echo "$WOWDECOMP_ACORE_BIN"
        return 0
    fi
    local candidate
    for candidate in \
        "$ROOT/test/local-server/azerothcore-wotlk/env/dist/bin" \
        "$HOME/azerothcore/env/dist/bin" \
        "$HOME/AzerothCore/env/dist/bin" \
        "$HOME/Repos/azerothcore-wotlk/env/dist/bin" \
        "$HOME/Repos/AzerothCore/env/dist/bin"
    do
        if [[ -x "$candidate/authserver" && -x "$candidate/worldserver" ]]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

resolve_config_dir() {
    if [[ -n "${WOWDECOMP_ACORE_CONFIG_DIR:-}" ]]; then
        echo "$WOWDECOMP_ACORE_CONFIG_DIR"
        return 0
    fi
    local bin="$1"
    for candidate in \
        "$(dirname "$bin")/etc" \
        "$(dirname "$bin")/../etc" \
        "$ROOT/test/local-server/acore-config"
    do
        if [[ -f "$candidate/authserver.conf" && -f "$candidate/worldserver.conf" ]]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

BIN="$(resolve_acore_bin)" || {
    echo "No AzerothCore authserver/worldserver found." >&2
    echo "Set WOWDECOMP_ACORE_BIN to the directory containing both binaries." >&2
    echo "Example: export WOWDECOMP_ACORE_BIN=\$HOME/azerothcore/env/dist/bin" >&2
    exit 1
}

CONFIG="$(resolve_config_dir "$BIN")" || {
    echo "No authserver.conf / worldserver.conf next to your AC install." >&2
    echo "Set WOWDECOMP_ACORE_CONFIG_DIR to the folder with those configs." >&2
    exit 1
}

mkdir -p "$PID_DIR" "$LOG_DIR"

port_in_use() {
    ss -ltn | grep -q ":$1 "
}

AUTH_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --auth-only) AUTH_ONLY=1 ;;
    esac
done

if port_in_use 3724 || pgrep -f "authserver -c .*azerothcore-wotlk/env/dist" >/dev/null 2>&1; then
    echo "auth already up on :3724"
else
    echo "Starting authserver from $BIN (config $CONFIG)"
    (cd "$BIN" && ./authserver -c "$CONFIG/authserver.conf" </dev/null) >>"$LOG_DIR/authserver.log" 2>&1 &
    echo $! >"$PID_DIR/authserver.pid"
fi

if [[ "$AUTH_ONLY" -eq 0 ]]; then
    if port_in_use 8085 || pgrep -f "worldserver -c .*azerothcore-wotlk/env/dist" >/dev/null 2>&1; then
        echo "world already up on :8085 (use console.sh for interactive world)"
    else
        echo "Starting worldserver from $BIN (config $CONFIG)"
        (cd "$BIN" && ./worldserver -c "$CONFIG/worldserver.conf" </dev/null) >>"$LOG_DIR/worldserver.log" 2>&1 &
        echo $! >"$PID_DIR/worldserver.pid"
    fi
fi

if [[ "${WOWDECOMP_ACORE_QUIET:-0}" == 1 ]]; then
    exit 0
fi

cat <<EOF

AzerothCore (native)
  Bin:     $BIN
  Config:  $CONFIG
  Logs:    $LOG_DIR
  Auth:    127.0.0.1:3724 (default)
  World:   127.0.0.1:8085 (default)

Interactive AC> console (account create, etc.):
  $ROOT/Tools/local-server/stop.sh    # if worldserver was started in background
  $ROOT/Tools/local-server/console.sh

Background logs (not a shell): $LOG_DIR/worldserver.log

Stop: $ROOT/Tools/local-server/stop.sh
EOF
