#!/usr/bin/env bash
# ʕ •ᴥ•ʔ✿ Local 3.3.5a station: WASM packages, symlinked play scope, headless tests, launch ✿ ʕ •ᴥ•ʔ
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GAME_SRC="${WOWDECOMP_GAME_SRC:-$HOME/Games/WoW335}"
PLAY_DIR="${WOWDECOMP_PLAY_DIR:-$ROOT/test/dev-335}"
BUILD_DIR="${WOWDECOMP_BUILD_DIR:-$ROOT/build/linux-dev}"
RELEASE_BUILD_DIR="${WOWDECOMP_RELEASE_BUILD_DIR:-$ROOT/build/linux-release}"
RELEASE_BIN="$ROOT/bin/Linux/WowClient64.release"
REALM_HOST="${WOWDECOMP_REALM_HOST:-127.0.0.1}"
PROFILE_SHM="/dev/shm/wowdecomp-profile"

# ʕ ◕ᴥ◕ ʔ✿ Live client scope. loop/memory-bomb trap or exhaust the host on load ✿ ʕ ◕ᴥ◕ ʔ
# ʕ •ᴥ•ʔ✿ ElvUI and Details are whole addon trees staged as their own WasmAddOns folders ✿ ʕ •ᴥ•ʔ
LIVE_ADDONS=(
    c-hello
    ping
    storage-test
    payload-test
    unit-auction-test
    confirm-test
    host-edge
    wim
    lua-parity
)

ELVUI_SRC="${WOWDECOMP_ELVUI:-/run/media/emma/WOW/Repos/ElvUI_Outlawcraft}"
DETAILS_SRC="${WOWDECOMP_DETAILS:-$HOME/Repos/Details-WotLK}"

usage() {
    cat <<EOF
Usage: $0 [all|deps|addons|scope|build|test|launch|release|smoke]

  all      deps, WASM packages, symlinked scope, client/host build, tests, smoke launch
  deps     wasm32 rust std + clang wasm linker
  addons   build Addons/Wasm into Addons/Wasm/dist
  scope    symlink $PLAY_DIR from $GAME_SRC and the built packages
  build    Ninja RelWithDebInfo (-O2 -g -DNDEBUG) WowClient, wasm_addon_host, wasm_addon_tests
  test     headless host tests against Addons/Wasm/dist
  launch   start AzerothCore in Docker, then the RelWithDebInfo WowClient64 in the dev scope
  release  build Release (-O3 -DNDEBUG) into a separate tree and launch a play-dir copy of that binary
  smoke    start the client, require it to stay up, then stop it

Env: WOWDECOMP_GAME_SRC WOWDECOMP_PLAY_DIR WOWDECOMP_BUILD_DIR WOWDECOMP_RELEASE_BUILD_DIR WOWDECOMP_REALM_HOST
     WOWDECOMP_PROFILE (on|off, default on; release defaults off — CPU zones in $PROFILE_SHM)
EOF
}

# ʕ •ᴥ•ʔ✿ In-game Profiler (Engine/Source/Base/Profiler.cpp) publishes frames for Tools/profile-mcp ✿ ʕ •ᴥ•ʔ
apply_profiler_env() {
    case "${WOWDECOMP_PROFILE:-on}" in
        0|off|false|no|disable)
            export WOW_PROFILE=0
            ;;
        *)
            unset WOW_PROFILE 2>/dev/null || true
            ;;
    esac
}

profiler_launch_note() {
    if [[ "${WOW_PROFILE:-}" == "0" ]]; then
        return
    fi
    echo "CPU profiler: $PROFILE_SHM (Cursor MCP wowdecomp-profile; WOWDECOMP_PROFILE=off to disable)"
}

# ʕ •ᴥ•ʔ✿ Arch rustc rejects upstream wasm32 std, so addon builds use a user rustup toolchain ✿ ʕ •ᴥ•ʔ
export PATH="${HOME}/.cargo/bin:${PATH}"

wasm32_std_ready() {
    local dir
    dir="$(rustc --print target-libdir --target wasm32-unknown-unknown 2>/dev/null || true)"
    [[ -n "$dir" ]] && compgen -G "$dir/libstd-*.rlib" >/dev/null
}

ensure_wasm32_std() {
    if [[ ! -x "${HOME}/.cargo/bin/rustup" ]]; then
        curl --proto '=https' --tlsv1.2 -fsSL https://sh.rustup.rs | \
            sh -s -- -y --default-toolchain stable --profile minimal --component rust-std --target wasm32-unknown-unknown
    fi
    export PATH="${HOME}/.cargo/bin:${PATH}"
    if ! wasm32_std_ready; then
        rustup target add wasm32-unknown-unknown
    fi
    wasm32_std_ready
}

ensure_packages() {
    local missing=()
    local pkg
    for pkg in cmake ninja clang lld curl sdl2 libx11 alsa-lib xvidcore vulkan-headers vulkan-icd-loader; do
        pacman -Q "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        echo "Installing: ${missing[*]}"
        sudo pacman -S --needed --noconfirm "${missing[@]}"
    fi
    ensure_wasm32_std
    local probe
    probe="$(mktemp --suffix=.wasm)"
    if ! clang --target=wasm32-unknown-unknown -nostdlib -Wl,--no-entry -Wl,--export=wow_on_load -x c -o "$probe" - <<<'int wow_on_load(void){return 0;}' >/dev/null 2>&1; then
        rm -f "$probe"
        echo "clang cannot link wasm32 (need lld / wasm-ld)" >&2
        exit 1
    fi
    rm -f "$probe"
}

build_addons() {
    ensure_wasm32_std
    "$ROOT/Addons/Wasm/build.sh"
    sh "$ROOT/Addons/Wasm/examples/c-hello/build.sh"
}

link_scope() {
    if [[ ! -d "$GAME_SRC/Data" ]]; then
        echo "No 3.3.5a Data/ in $GAME_SRC" >&2
        exit 1
    fi
    WOWDECOMP_PLAY_DIR="$PLAY_DIR" WOWDECOMP_REALM_HOST="$REALM_HOST" \
        "$ROOT/Tools/setup-linux-test-client.sh" --vanilla "$GAME_SRC"

    local wasm_root="$PLAY_DIR/Interface/WasmAddOns"
    rm -rf "$wasm_root"
    mkdir -p "$wasm_root" "$PLAY_DIR/Interface/AddOns/WasmInspector"
    local name
    for name in "${LIVE_ADDONS[@]}"; do
        if [[ -f "$ROOT/Addons/Wasm/dist/$name/addon.toml" && -f "$ROOT/Addons/Wasm/dist/$name/addon.wasm" ]]; then
            ln -sfn "$ROOT/Addons/Wasm/dist/$name" "$wasm_root/$name"
        elif [[ "$name" == "c-hello" && -f "$ROOT/Addons/Wasm/examples/c-hello/addon.wasm" ]]; then
            mkdir -p "$ROOT/Addons/Wasm/dist/c-hello"
            cp -f "$ROOT/Addons/Wasm/examples/c-hello/addon.wasm" "$ROOT/Addons/Wasm/examples/c-hello/addon.toml" "$ROOT/Addons/Wasm/dist/c-hello/"
            ln -sfn "$ROOT/Addons/Wasm/dist/c-hello" "$wasm_root/$name"
        fi
    done
    cp -f "$ROOT/Addons/Lua/WasmInspector/WasmInspector.toc" \
        "$ROOT/Addons/Lua/WasmInspector/WasmInspector.lua" \
        "$PLAY_DIR/Interface/AddOns/WasmInspector/"
    stage_wasm_game_addons "$wasm_root"
    echo "Dev scope: $PLAY_DIR"
    echo "WasmAddOns -> $wasm_root ($(find "$wasm_root" -mindepth 1 -maxdepth 1 -type l -o -mindepth 1 -maxdepth 1 -type d | wc -l) entries)"
}

# ʕ •ᴥ•ʔ✿ One folder per addon: the original tree, plus a wasm manifest, under Interface/WasmAddOns/<Name> ✿ ʕ •ᴥ•ʔ
stage_wasm_game_addons() {
    local wasm_root="$1"
    local host_wasm="$ROOT/Addons/Wasm/toc-host/addon.wasm"
    if [[ ! -f "$host_wasm" ]]; then
        sh "$ROOT/Addons/Wasm/toc-host/build.sh"
    fi
    if [[ ! -d "$DETAILS_SRC/Details" && -d /tmp/Details-WotLK/Details ]]; then
        DETAILS_SRC=/tmp/Details-WotLK
    fi
    stage_one() {
        local src="$1" name="$2" id="$3" title="$4"
        local dest="$wasm_root/$name"
        local item base
        rm -rf "$dest"
        mkdir -p "$dest"
        for item in "$src"/*; do
            [[ -e "$item" ]] || continue
            base="$(basename "$item")"
            ln -sfn "$item" "$dest/$base"
        done
        cp -f "$host_wasm" "$dest/addon.wasm"
        cat > "$dest/addon.toml" <<EOF
[addon]
id = "$id"
name = "$title"
version = "1"
api_version = "1"
EOF
        echo "package $name"
    }
    if [[ -d "$ELVUI_SRC/ElvUI" ]]; then
        stage_one "$ELVUI_SRC/ElvUI" ElvUI game.elvui "ElvUI"
        stage_one "$ELVUI_SRC/ElvUI_OptionsUI" ElvUI_OptionsUI game.elvui-options "ElvUI Options"
        stage_one "$ELVUI_SRC/ElvUI_AddOnSkins" ElvUI_AddOnSkins game.elvui-addonskins "ElvUI AddOnSkins"
        stage_one "$ELVUI_SRC/ElvUI_DTBars2" ElvUI_DTBars2 game.elvui-dtbars2 "ElvUI DTBars2"
        stage_one "$ELVUI_SRC/ElvUI_Enhanced" ElvUI_Enhanced game.elvui-enhanced "ElvUI Enhanced"
        stage_one "$ELVUI_SRC/ElvUI_EnhancedFriendsList" ElvUI_EnhancedFriendsList game.elvui-friends "ElvUI EnhancedFriendsList"
        stage_one "$ELVUI_SRC/ElvUI_ExtraActionBars" ElvUI_ExtraActionBars game.elvui-extrabars "ElvUI ExtraActionBars"
        stage_one "$ELVUI_SRC/ElvUI_PartyDamage" ElvUI_PartyDamage game.elvui-partydamage "ElvUI PartyDamage"
    else
        echo "No ElvUI tree at $ELVUI_SRC" >&2
    fi
    if [[ -d "$DETAILS_SRC/Details" ]]; then
        stage_one "$DETAILS_SRC/Details" Details game.details "Details"
        stage_one "$DETAILS_SRC/Details_3DModelsPaths" Details_3DModelsPaths game.details-models "Details 3DModelsPaths"
        stage_one "$DETAILS_SRC/Details_ChartViewer" Details_ChartViewer game.details-charts "Details ChartViewer"
        stage_one "$DETAILS_SRC/Details_DataStorage" Details_DataStorage game.details-storage "Details DataStorage"
        stage_one "$DETAILS_SRC/Details_DeathGraphs" Details_DeathGraphs game.details-deaths "Details DeathGraphs"
        stage_one "$DETAILS_SRC/Details_EncounterDetails" Details_EncounterDetails game.details-encounters "Details EncounterDetails"
        stage_one "$DETAILS_SRC/Details_SunderCount" Details_SunderCount game.details-sunder "Details SunderCount"
        stage_one "$DETAILS_SRC/Details_TimeLine" Details_TimeLine game.details-timeline "Details TimeLine"
        stage_one "$DETAILS_SRC/Details_TinyThreat" Details_TinyThreat game.details-threat "Details TinyThreat"
    else
        echo "No Details tree at $DETAILS_SRC" >&2
    fi
}

configure_build() {
    cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON
}

configure_release() {
    cmake -S "$ROOT" -B "$RELEASE_BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON \
        -DWOWCLIENT_OUTPUT_OVERRIDE=WowClient64.release
}

build_release() {
    configure_release
    cmake --build "$RELEASE_BUILD_DIR" --target WowClient -j "$(nproc)"
}

build_client() {
    configure_build
    cmake --build "$BUILD_DIR" --target WowClient wasm_addon_host wasm_addon_tests -j "$(nproc)"
    WOWDECOMP_PLAY_DIR="$PLAY_DIR" WOWDECOMP_REALM_HOST="$REALM_HOST" \
        "$ROOT/Tools/setup-linux-test-client.sh" --vanilla "$GAME_SRC" >/dev/null
}

run_tests() {
    local host="$BUILD_DIR/bin/wasm_addon_tests"
    if [[ ! -x "$host" ]]; then
        configure_build
        cmake --build "$BUILD_DIR" --target wasm_addon_host wasm_addon_tests -j "$(nproc)"
    fi
    "$BUILD_DIR/bin/wasm_addon_tests" "$ROOT/Addons/Wasm/dist"
    "$BUILD_DIR/bin/wasm_addon_host" "$ROOT/Addons/Wasm/dist/hello-world" --event PLAYER_LOGIN --inspector
}

launch_client() {
    if [[ ! -x "$PLAY_DIR/WowClient64" ]]; then
        echo "Missing $PLAY_DIR/WowClient64 — run: $0 build" >&2
        exit 1
    fi
    WOWDECOMP_SERVER_BACKEND=docker "$ROOT/Tools/local-server/start.sh"
    apply_profiler_env
    profiler_launch_note
    echo "RelWithDebInfo (-O2 -g -DNDEBUG): $PLAY_DIR/WowClient64"
    cd "$PLAY_DIR"
    exec ./WowClient64
}

launch_release() {
    build_release
    if [[ ! -x "$RELEASE_BIN" ]]; then
        echo "Missing $RELEASE_BIN — release build did not produce it" >&2
        exit 1
    fi
    if [[ ! -d "$PLAY_DIR/Data" ]]; then
        echo "Missing $PLAY_DIR/Data — run: $0 scope" >&2
        exit 1
    fi
    WOWDECOMP_SERVER_BACKEND=docker "$ROOT/Tools/local-server/start.sh"
    # ʕ •ᴥ•ʔ✿ Release play is the fps path. Leave the zone sampler off unless asked ✿ ʕ •ᴥ•ʔ
    : "${WOWDECOMP_PROFILE:=off}"
    apply_profiler_env
    profiler_launch_note
    # ʕ •ᴥ•ʔ✿ /proc/self/exe is the Data root. A symlink resolves back to bin/Linux ✿ ʕ •ᴥ•ʔ
    local play_bin="$PLAY_DIR/WowClient64.release"
    cp -fl "$RELEASE_BIN" "$play_bin" 2>/dev/null || cp -f "$RELEASE_BIN" "$play_bin"
    echo "Release (-O3 -DNDEBUG): $play_bin"
    cd "$PLAY_DIR"
    exec ./WowClient64.release
}

smoke_launch() {
    if [[ ! -x "$PLAY_DIR/WowClient64" ]]; then
        echo "Missing $PLAY_DIR/WowClient64 — run: $0 build" >&2
        exit 1
    fi
    mkdir -p "$PLAY_DIR/Logs"
    local log="$PLAY_DIR/Logs/dev-station-launch.log"
    : >"$log"
    apply_profiler_env
    set +e
    (cd "$PLAY_DIR" && ./WowClient64) >"$log" 2>&1 &
    local pid=$!
    local i alive=0
    for i in 1 2 3 4 5 6 7 8; do
        if kill -0 "$pid" 2>/dev/null; then
            alive=1
            sleep 1
        else
            alive=0
            break
        fi
    done
    if [[ "$alive" -eq 1 ]]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
        set -e
        echo "Smoke launch stayed up; stopped pid $pid"
        return 0
    fi
    wait "$pid" 2>/dev/null || true
    set -e
    echo "Client exited during smoke launch. Log: $log" >&2
    tail -n 40 "$log" >&2 || true
    exit 1
}

cmd="${1:-all}"
case "$cmd" in
    -h|--help|help) usage ;;
    deps) ensure_packages ;;
    addons) build_addons ;;
    scope) link_scope ;;
    build) build_client ;;
    test) run_tests ;;
    launch) launch_client ;;
    release) launch_release ;;
    smoke) smoke_launch ;;
    all)
        ensure_packages
        build_addons
        link_scope
        build_client
        run_tests
        smoke_launch
        ;;
    *)
        usage >&2
        exit 1
        ;;
esac
