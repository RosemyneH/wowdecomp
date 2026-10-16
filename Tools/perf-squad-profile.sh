#!/usr/bin/env bash
# ʕ •ᴥ•ʔ✿ One outdoor steady sample: build client, log in a:a, perf stat in world ✿ ʕ •ᴥ•ʔ
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLAY="${WOWDECOMP_PLAY_DIR:-$ROOT/test/dev-335}"
BUILD="${WOWDECOMP_BUILD_DIR:-$ROOT/build/linux-dev}"
OUT="${WOWDECOMP_PROFILE_OUT:-$ROOT/docs/perf-squads/log/profile-run.md}"
STEADY_SEC="${WOWDECOMP_PROFILE_STEADY_SEC:-8}"

if [[ ! -x "$PLAY/WowClient64" ]]; then
    "$ROOT/Tools/dev-station.sh" build
fi

python3 "$ROOT/Tools/local-server/acore-account.py" a a --check 127.0.0.1:3724 >/dev/null

pgrep -x ydotoold >/dev/null || ydotoold &

LOG="$(mktemp)"
: >"$LOG"
cd "$PLAY"
./WowClient64 -windowed -console -consolelog >>"$LOG" 2>&1 &
WPID=$!

cleanup() {
    kill "$WPID" 2>/dev/null || true
    wait "$WPID" 2>/dev/null || true
}
trap cleanup EXIT

sleep 10
ydotool type 'a'
sleep 0.2
ydotool key 15:1 15:0
sleep 0.2
ydotool type 'a'
sleep 0.2
ydotool key 28:1 28:0

for _ in $(seq 1 30); do
    if rg -q 'Weather changed|WORLD_ENTER|Entering world|Gamespeed set' "$LOG" 2>/dev/null; then
        break
    fi
    sleep 1
done
sleep 3

# Console: frozen outdoor machine from docs/perf-squads/log/scene.md
ydotool key 41:1 41:0
sleep 0.3
ydotool type 'gxApi Vulkan'
ydotool key 28:1 28:0
sleep 0.5
for line in \
    'farclip 350' \
    'groundEffectDist 70' \
    'groundEffectDensity 16' \
    'cameraDistanceMax 15' \
    'cameraSavedDistance 15' \
    'camerasmooth 0' \
    'cameraBobbing 0' \
    'M2BatchDoodads 1' \
    'nameplateShowEnemies 0' \
    'nameplateShowFriends 0'; do
    ydotool key 41:1 41:0
    sleep 0.15
    ydotool type "$line"
    ydotool key 28:1 28:0
    sleep 0.2
done

sleep 5

sample_cpu() {
    local pid=$1 sec=$2
    local hz
    hz=$(getconf CLK_TCK)
    read -r ut1 st1 < <(awk '{print $14,$15}' "/proc/$pid/stat")
    sleep "$sec"
    read -r ut2 st2 < <(awk '{print $14,$15}' "/proc/$pid/stat")
    awk -v hz="$hz" -v u1="$ut1" -v s1="$st1" -v u2="$ut2" -v s2="$st2" -v sec="$sec" '
        BEGIN {
            cpu = ((u2-u1)+(s2-s1)) / hz / sec * 100;
            printf "sample_seconds=%s user_stime_delta=%d jiffies (~%.1f%% of one core)\n", sec, (u2-u1)+(s2-s1), cpu;
        }'
}

PERF="$(mktemp)"
if command -v perf >/dev/null; then
    perf stat -p "$WPID" -e cycles,instructions,cache-misses,task-clock -- sleep "$STEADY_SEC" 2>"$PERF" || true
else
    echo "perf not installed; using /proc stat sample" >"$PERF"
    sample_cpu "$WPID" "$STEADY_SEC" >>"$PERF"
fi

REBAR="$(rg 'vulkan memory:' "$LOG" | tail -1 || true)"
GX="$(rg 'GxApi:' "$LOG" | tail -1 || true)"
LOGIN="$(rg 'LOGIN_OK|CHAR_LOGIN' "$LOG" | tail -3 || true)"
WEATHER="$(rg 'Weather changed' "$LOG" | wc -l)"

mkdir -p "$(dirname "$OUT")"
{
    echo "# Profile run — accountant input"
    echo
    echo "Captured $(date -u +%Y-%m-%dT%H:%MZ) UTC. Account **a:a** on \`127.0.0.1\`. Tree **$(git -C "$ROOT" rev-parse --short HEAD)**."
    echo
    echo "## Build"
    echo
    echo "- Client: \`$PLAY/WowClient64\`"
    echo "- Recent slices: \`731e9985\` (M2 skin overlaps terrain draw), \`cab3ee2f\` (ReBAR frame buffers)."
    echo
    echo "## Machine (stderr)"
    echo
    echo '```'
    echo "${REBAR:-no vulkan memory line}"
    echo "${GX:-no GxApi line}"
    echo '```'
    echo
    echo "## Session"
    echo
    echo '```'
    echo "${LOGIN:-no login lines}"
    echo "weather-tick lines in log: $WEATHER"
    echo '```'
    echo
    echo "## Steady window (\`${STEADY_SEC}s\` dismounted, post-cvar, CPU sample on WowClient64 pid)"
    echo
    echo '```'
    cat "$PERF"
    echo '```'
    echo
    echo "## Accountant notes (no \`WOW_PERFLOG\` yet)"
    echo
    echo "- **ReBAR (\`cab3ee2f\`).** With \`rebar=on\`, non-bounce \`CreateBuffer\` prefers host-visible **device-local** when the type bit allows (\`VkContext.cpp:1052-1054\`). \`vk_alloc\` **count** per startup ring is unchanged (still six host-visible rings + stream chunks). **Traffic** may skip the upload heap for those buffers; textures and bounce staging stay on \`localType\` / \`uploadType\` (\`VkContext.cpp:1047-1060\`). Steady \`vk_alloc=0\` gate in \`loop.md\` still applies."
    echo "- **Skin overlap (\`731e9985\`).** \`M2SkinPrepare\` caps scratch at **64 MiB** (\`M2Scene.cpp:1839\`) and writes **\`32 × verts\`** per unique \`(model, section, texCoord, optimized)\` on workers (\`M2Scene.cpp:1786\`, \`Job::Start\` at \`:1857\`). \`M2SkinFinish\` joins before model draw (\`M2Scene.cpp:2396\`). Byte formulas in sections above are unchanged; wall time can hide skin under terrain until Campaign 0 prints \`zone_m2_skin_ns\` and \`zone_terrain_ns\` on the same line."
    echo "- **Next.** Land Campaign 0 (\`WOW_PERFLOG=1\`) and replace this \`perf stat\` block with one middle steady \`frame scene=...\` line per \`scene.md\`."
} >"$OUT"

echo "Wrote $OUT"
cat "$OUT"
