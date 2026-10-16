#!/usr/bin/env bash
# ʕ •ᴥ•ʔ✿ Local 3.3.5a station: WASM packages, symlinked play scope, headless tests, launch ✿ ʕ •ᴥ•ʔ
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GAME_SRC="${WOWDECOMP_GAME_SRC:-$HOME/Games/WoW335}"
PLAY_DIR="${WOWDECOMP_PLAY_DIR:-$ROOT/test/dev-335}"
BUILD_DIR="${WOWDECOMP_BUILD_DIR:-$ROOT/build/linux-dev}"
REALM_HOST="${WOWDECOMP_REALM_HOST:-127.0.0.1}"

# ʕ ◕ᴥ◕ ʔ✿ Live client scope. loop/memory-bomb trap or exhaust the host on load ✿ ʕ ◕ᴥ◕ ʔ
# ʕ •ᴥ•ʔ✿ ElvUI and Details load as the original Interface/AddOns trees, not these crates ✿ ʕ •ᴥ•ʔ
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
Usage: $0 [all|deps|addons|scope|build|test|launch|smoke]

  all      deps, WASM packages, symlinked scope, client/host build, tests, smoke launch
  deps     wasm32 rust std + clang wasm linker
  addons   build Addons/Wasm into Addons/Wasm/dist
  scope    symlink $PLAY_DIR from $GAME_SRC and the built packages
  build    Ninja build of WowClient, wasm_addon_host, wasm_addon_tests
  test     headless host tests against Addons/Wasm/dist
  launch   start AzerothCore in Docker, then WowClient64 in the dev scope
  smoke    start the client, require it to stay up, then stop it

Env: WOWDECOMP_GAME_SRC WOWDECOMP_PLAY_DIR WOWDECOMP_BUILD_DIR WOWDECOMP_REALM_HOST
EOF
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
    link_game_addons
    echo "Dev scope: $PLAY_DIR"
    echo "WasmAddOns -> $wasm_root ($(find "$wasm_root" -mindepth 1 -maxdepth 1 -type l | wc -l) links)"
}

# ʕ •ᴥ•ʔ✿ The play scope runs the original addons. Folder names match their texture paths ✿ ʕ •ᴥ•ʔ
link_game_addons() {
    local addons="$PLAY_DIR/Interface/AddOns"
    mkdir -p "$addons"
    local name
    if [[ -d "$ELVUI_SRC/ElvUI" ]]; then
        for name in ElvUI ElvUI_OptionsUI ElvUI_AddOnSkins ElvUI_DTBars2 ElvUI_Enhanced ElvUI_EnhancedFriendsList ElvUI_ExtraActionBars ElvUI_PartyDamage; do
            if [[ -d "$ELVUI_SRC/$name" ]]; then
                ln -sfn "$ELVUI_SRC/$name" "$addons/$name"
            fi
        done
        echo "ElvUI -> $ELVUI_SRC"
    else
        echo "No ElvUI tree at $ELVUI_SRC" >&2
    fi
    if [[ ! -d "$DETAILS_SRC/Details" && -d /tmp/Details-WotLK/Details ]]; then
        DETAILS_SRC=/tmp/Details-WotLK
    fi
    if [[ -d "$DETAILS_SRC/Details" ]]; then
        for name in Details Details_3DModelsPaths Details_ChartViewer Details_DataStorage Details_DeathGraphs Details_EncounterDetails Details_SunderCount Details_TimeLine Details_TinyThreat; do
            if [[ -d "$DETAILS_SRC/$name" ]]; then
                ln -sfn "$DETAILS_SRC/$name" "$addons/$name"
            fi
        done
        echo "Details -> $DETAILS_SRC"
    else
        echo "No Details tree at $DETAILS_SRC" >&2
    fi
}

configure_build() {
    cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON
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
    cd "$PLAY_DIR"
    exec ./WowClient64
}

smoke_launch() {
    if [[ ! -x "$PLAY_DIR/WowClient64" ]]; then
        echo "Missing $PLAY_DIR/WowClient64 — run: $0 build" >&2
        exit 1
    fi
    mkdir -p "$PLAY_DIR/Logs"
    local log="$PLAY_DIR/Logs/dev-station-launch.log"
    : >"$log"
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
