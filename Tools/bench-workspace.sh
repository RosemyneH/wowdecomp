#!/usr/bin/env bash
# ʕ •ᴥ•ʔ✿ Log a:a into the bench client on sway workspace 7 and sample the profiler ✿ ʕ •ᴥ•ʔ
# The window title is wowdecomp-bench. It is moved, not focused, so another workspace can stay in front.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLAY="${WOWDECOMP_PLAY_DIR:-$ROOT/test/dev-335}"
BIN="${WOWDECOMP_BENCH_BIN:-$ROOT/bin/Linux/WowClient64.release}"
LOG_DIR="$PLAY/Logs"
LOG="$LOG_DIR/bench-workspace.log"
PIDFILE="$LOG_DIR/bench-workspace.pid"
SHM="/dev/shm/wowdecomp-profile"
TITLE="${WOWDECOMP_WINDOW_TITLE:-wowdecomp-bench}"
WORKSPACE="${WOWDECOMP_BENCH_WORKSPACE:-7}"
AUTOLOGIN="${WOWDECOMP_AUTOLOGIN:-a:a:Adwa}"
SAMPLE_FRAMES="${WOWDECOMP_BENCH_FRAMES:-40}"
INTERVAL="${WOWDECOMP_BENCH_INTERVAL:-20}"

mkdir -p "$LOG_DIR"

ensure_awesome_cvars() {
    local cfg="$PLAY/WTF/Config.wtf"
    [[ -f "$cfg" ]] || return 0
    local line
    for line in \
        'SET cameraIndirectVisibility "0"' \
        'SET nameplateStacking "1"' \
        'SET nameplateDistance "41"' \
        'SET nameplateStackFriendly "1"' \
        'SET MSDFMode "1"' \
        'SET showPlayer "1"' \
        'SET interactionMode "1"' \
        'SET interactionAngle "60"'
    do
        local key
        key="$(awk '{print $2}' <<<"$line")"
        if ! grep -q "^SET ${key} " "$cfg"; then
            printf '%s\n' "$line" >>"$cfg"
        fi
    done
}

sway_rule() {
    command -v swaymsg >/dev/null || return 0
    # -- ʕ •ᴥ•ʔ✿ Workspace 7 otherwise lands on HDMI. The bench has to fill the ultrawide. ✿ ʕ •ᴥ•ʔ
    swaymsg "workspace ${WORKSPACE} output DP-1" >/dev/null
    swaymsg "for_window [title=\"${TITLE}\"] move container to workspace number ${WORKSPACE}" >/dev/null
}

park_window() {
    command -v swaymsg >/dev/null || return 0
    local pid="$1"
    swaymsg "workspace ${WORKSPACE} output DP-1" >/dev/null 2>&1 || true
    swaymsg "[pid=${pid}] move container to workspace number ${WORKSPACE}" >/dev/null 2>&1 || true
    swaymsg "[pid=${pid}] fullscreen enable" >/dev/null 2>&1 || true
}

enable_profile() {
    python3 - "$SHM" <<'PY'
import os, struct, sys, time
path = sys.argv[1]
for _ in range(50):
    if os.path.exists(path) and os.path.getsize(path) >= 24:
        break
    time.sleep(0.1)
else:
    sys.exit(0)
fd = os.open(path, os.O_RDWR)
try:
    os.pwrite(fd, struct.pack("<I", 1), 20)
finally:
    os.close(fd)
PY
}

sample_profile() {
    python3 - "$SHM" "$SAMPLE_FRAMES" <<'PY'
import os, struct, sys
path, count = sys.argv[1], int(sys.argv[2])
if not os.path.exists(path):
    print("no-shm")
    sys.exit(0)
fd = os.open(path, os.O_RDONLY)
data = os.read(fd, os.path.getsize(path))
os.close(fd)
if len(data) < 264 or struct.unpack_from("<I", data, 0)[0] != 0x46525057:
    print("shm-unready")
    sys.exit(0)
zones, clocks, enabled = struct.unpack_from("<I", data, 8)[0], struct.unpack_from("<Q", data, 24)[0], struct.unpack_from("<I", data, 20)[0]
clocks = clocks or 1
names = []
for i in range(7):
    raw = data[40 + i * 32:40 + (i + 1) * 32]
    names.append(raw.split(b"\0", 1)[0].decode("utf-8", "replace"))
frame_size = 16 + 24 * 7
rows = []
for i in range(120):
    off = 264 + i * frame_size
    ticks, serial, _pad = struct.unpack_from("<QII", data, off)
    if ticks:
        rows.append((serial, ticks))
rows.sort()
rows = rows[-count:]
if not rows:
    print(f"enabled={enabled} frames=0")
    sys.exit(0)
mean = sum(t for _s, t in rows) / len(rows) / clocks * 1000.0
print(f"enabled={enabled} frames={len(rows)} mean_ms={mean:.2f} fps={1000.0 / mean if mean else 0:.1f}")
PY
}

client_running() {
    [[ -f "$PIDFILE" ]] || return 1
    local pid
    pid="$(cat "$PIDFILE")"
    [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null
}

stop_client() {
    if client_running; then
        local pid
        pid="$(cat "$PIDFILE")"
        kill "$pid" 2>/dev/null || true
        for _ in $(seq 1 30); do
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.2
        done
        kill -9 "$pid" 2>/dev/null || true
    fi
    rm -f "$PIDFILE"
}

start_client() {
    if [[ ! -x "$BIN" ]]; then
        echo "missing $BIN" >&2
        exit 1
    fi
    # -- ʕ •ᴥ•ʔ✿ A hardlink is the build output. The linker truncates it and the live client jumps to null. ✿ ʕ •ᴥ•ʔ
    cp -f "$BIN" "$PLAY/WowClient64.release"
    ensure_awesome_cvars
    sway_rule
    (
        cd "$PLAY"
        unset WOW_PROFILE
        export WOWDECOMP_AUTOLOGIN="$AUTOLOGIN"
        export WOWDECOMP_WINDOW_TITLE="$TITLE"
        exec ./WowClient64.release -windowed
    ) >>"$LOG" 2>&1 &
    echo $! >"$PIDFILE"
    local pid
    pid="$(cat "$PIDFILE")"
    for _ in $(seq 1 40); do
        park_window "$pid"
        sleep 0.25
    done
    enable_profile || true
    echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) start pid=$pid login=$AUTOLOGIN workspace=$WORKSPACE" | tee -a "$LOG"
}

sample_once() {
    if ! client_running; then
        echo "bench client is not running" >&2
        exit 1
    fi
    local pid line
    pid="$(cat "$PIDFILE")"
    park_window "$pid"
    enable_profile || true
    line="$(sample_profile)"
    echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) pid=$pid $line" | tee -a "$LOG"
}

loop_samples() {
    local started=""
    while true; do
        if ! client_running; then
            start_client
            started="$(stat -c %Y "$BIN")"
            sleep 8
        fi
        local pid now
        pid="$(cat "$PIDFILE")"
        now="$(stat -c %Y "$BIN")"
        if [[ -n "$started" && "$now" != "$started" ]]; then
            echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) binary changed, relaunch" | tee -a "$LOG"
            stop_client
            continue
        fi
        started="$now"
        park_window "$pid"
        enable_profile || true
        echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) pid=$pid $(sample_profile)" | tee -a "$LOG"
        sleep "$INTERVAL"
    done
}

case "${1:-loop}" in
    start) start_client ;;
    sample) sample_once ;;
    stop) stop_client ;;
    loop) loop_samples ;;
    *)
        echo "Usage: $0 [loop|start|sample|stop]" >&2
        exit 2
        ;;
esac
