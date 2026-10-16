#!/usr/bin/env bash
set -euo pipefail

AC_MARKER="${WOWDECOMP_ACORE_MARKER:-azerothcore-wotlk/env/dist}"

kill_port() {
    local port="$1"
    local line pid
    while read -r line; do
        [[ "$line" =~ pid=([0-9]+) ]] || continue
        pid="${BASH_REMATCH[1]}"
        kill "$pid" 2>/dev/null || true
    done < <(ss -ltnp 2>/dev/null | grep ":${port} " || true)
}

kill_by_marker() {
    local role="$1"
    pkill -f "${role} -c .*${AC_MARKER}" 2>/dev/null || true
}

kill_world() {
    kill_port 8085
    kill_by_marker worldserver
}

kill_auth() {
    kill_port 3724
    kill_by_marker authserver
}

case "${1:-all}" in
    world) kill_world ;;
    auth) kill_auth ;;
    all)
        kill_world
        kill_auth
        ;;
    *)
        echo "usage: $0 [world|auth|all]" >&2
        exit 1
        ;;
esac

sleep 1
