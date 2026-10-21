#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLAY_DIR="${WOWDECOMP_PLAY_DIR:-$ROOT/test/play-335}"
GAME_SRC=""
LOCAL_REALM=1
RETAIL_WTF=0
VANILLA=1
KEEP_WTF=1
REALM_HOST="${WOWDECOMP_REALM_HOST:-127.0.0.1}"

usage() {
    echo "Usage: $0 [--vanilla|--full-install] [--retail] [--use-game-wtf] [--keep-wtf] [/path/to/3.3.5a/client]" >&2
    echo "  Default (--vanilla): MPQs only; local Cache/Interface/Logs (no wxl, no symlinked addons)" >&2
    echo "  --full-install: also symlink Interface/Cache/Logs from the game folder (Astral-style)" >&2
    echo "  Default realm: $REALM_HOST (AzerothCore auth :3724)" >&2
    echo "  --retail: symlink retail WTF / realmlist from install" >&2
    echo "  --keep-wtf: do not replace WTF/ (keeps Account/* and last character). WOWDECOMP_RESET_WTF=1 forces a wipe" >&2
    exit 1
}

default_game_src() {
    local resolved
    # shellcheck source=game-src.sh
    source "$ROOT/Tools/game-src.sh"
    resolved="$(wowdecomp_default_game_src)"
    if [[ -d "$resolved/Data" ]]; then
        echo "$resolved"
        return
    fi
    if [[ -d "${HOME}/Games/WoW335/Data" ]]; then
        echo "${HOME}/Games/WoW335"
        return
    fi
    if [[ "$VANILLA" -eq 1 && -d "${HOME}/Games/client/Data" ]]; then
        echo "${HOME}/Games/client"
        return
    fi
    if [[ -d "${HOME}/Games/Astral/Data" ]]; then
        echo "${HOME}/Games/Astral"
        return
    fi
    if [[ -d "${HOME}/Games/client/Data" ]]; then
        echo "${HOME}/Games/client"
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --local-realm)
            LOCAL_REALM=1
            shift
            ;;
        --retail)
            LOCAL_REALM=0
            shift
            ;;
        --use-game-wtf)
            RETAIL_WTF=1
            shift
            ;;
        --vanilla)
            VANILLA=1
            shift
            ;;
        --full-install)
            VANILLA=0
            shift
            ;;
        --keep-wtf)
            KEEP_WTF=1
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            GAME_SRC="$1"
            shift
            ;;
    esac
done

if [[ -z "$GAME_SRC" ]]; then
    GAME_SRC="$(default_game_src)"
fi

if [[ ! -d "$GAME_SRC/Data" ]]; then
    echo "No Data/ in: $GAME_SRC" >&2
    usage
fi

mkdir -p "$PLAY_DIR" "$ROOT/bin/Linux"

if [[ -x "$ROOT/bin/Linux/WowClient64" ]]; then
    cp -fl "$ROOT/bin/Linux/WowClient64" "$PLAY_DIR/WowClient64" 2>/dev/null || cp -f "$ROOT/bin/Linux/WowClient64" "$PLAY_DIR/WowClient64"
else
    echo "WowClient64 is not built yet. Data scope will still be prepared." >&2
    echo "Build: cmake --build build/linux-dev --target WowClient -j\$(nproc)" >&2
fi
rm -f "$PLAY_DIR/Wow64"
if [[ -f "$ROOT/bin/Linux/Wow64" ]]; then
    cp -fl "$ROOT/bin/Linux/Wow64" "$PLAY_DIR/Wow64" 2>/dev/null || cp -f "$ROOT/bin/Linux/Wow64" "$PLAY_DIR/Wow64"
fi

link_data_tree() {
    local src="$1"
    local dst="$2"
    rm -rf "$dst"
    mkdir -p "$dst"
    for item in "$src"/*; do
        [[ -e "$item" ]] || continue
        ln -sfn "$item" "$dst/$(basename "$item")"
    done
}

link_data_with_local_realmlist() {
    local src="$GAME_SRC/Data"
    local dst="$PLAY_DIR/Data"
    local locale="${WOWDECOMP_LOCALE:-enUS}"
    rm -rf "$dst"
    mkdir -p "$dst"
    for item in "$src"/*; do
        [[ -e "$item" ]] || continue
        local base
        base="$(basename "$item")"
        if [[ "$base" == "$locale" ]]; then
            mkdir -p "$dst/$locale"
            for loc in "$item"/*; do
                [[ -e "$loc" ]] || continue
                local leaf
                leaf="$(basename "$loc")"
                if [[ "$leaf" == "realmlist.wtf" ]]; then
                    continue
                fi
                ln -sfn "$loc" "$dst/$locale/$leaf"
            done
            printf 'set realmlist "%s"\n' "${REALM_HOST}:3724" > "$dst/$locale/realmlist.wtf"
        else
            ln -sfn "$item" "$dst/$base"
        fi
    done
}

if [[ "$LOCAL_REALM" -eq 1 ]]; then
    link_data_with_local_realmlist
else
    link_data_tree "$GAME_SRC/Data" "$PLAY_DIR/Data"
fi

if [[ "$VANILLA" -eq 1 ]]; then
    # ʕ •ᴥ•ʔ✿ Drop full-install symlinks only; a real Interface dir holds the dev WasmAddOns scope ✿ ʕ •ᴥ•ʔ
    for name in Interface Cache Logs; do
        if [[ -L "$PLAY_DIR/$name" ]]; then
            rm -f "$PLAY_DIR/$name"
        fi
    done
    mkdir -p "$PLAY_DIR/Cache/WDB" "$PLAY_DIR/Logs" "$PLAY_DIR/Interface/FrameXML"
    rm -rf "$PLAY_DIR/WasmAddOns"
else
    for name in Interface Cache Logs; do
        if [[ -e "$GAME_SRC/$name" ]]; then
            ln -sfn "$GAME_SRC/$name" "$PLAY_DIR/$name"
        fi
    done
fi

if [[ "${WOWDECOMP_RESET_WTF:-}" == "1" ]]; then
    KEEP_WTF=0
fi

if [[ "$KEEP_WTF" -eq 1 && -d "$PLAY_DIR/WTF" ]]; then
    mkdir -p "$PLAY_DIR/WTF"
else
    rm -rf "$PLAY_DIR/WTF"
    mkdir -p "$PLAY_DIR/WTF"
    if [[ "$RETAIL_WTF" -eq 1 && -d "$GAME_SRC/WTF" ]]; then
        rm -rf "$PLAY_DIR/WTF"
        ln -sfn "$GAME_SRC/WTF" "$PLAY_DIR/WTF"
    elif [[ -d "$GAME_SRC/WTF" && "$LOCAL_REALM" -ne 1 && "$VANILLA" -ne 1 ]]; then
        rm -rf "$PLAY_DIR/WTF"
        ln -sfn "$GAME_SRC/WTF" "$PLAY_DIR/WTF"
    fi
fi

ensure_config_wtf() {
    local cfg="$PLAY_DIR/WTF/Config.wtf"
    if [[ -L "$PLAY_DIR/WTF" ]]; then
        return
    fi
    mkdir -p "$PLAY_DIR/WTF"
    touch "$cfg"
    if ! grep -q '^SET realmList ' "$cfg"; then
        printf 'SET realmList "%s"\n' "$REALM_HOST" >>"$cfg"
    fi
    if ! grep -q '^SET locale ' "$cfg"; then
        printf 'SET locale "enUS"\n' >>"$cfg"
    fi
    if [[ "$LOCAL_REALM" -eq 1 ]]; then
        for line in \
            'SET hwDetect "0"' \
            'SET readTOS "1"' \
            'SET readEULA "1"' \
            'SET cameraFov "100"'; do
            local key
            key="$(awk '{print $2}' <<<"$line")"
            if ! grep -q "^SET ${key} " "$cfg"; then
                printf '%s\n' "$line" >>"$cfg"
            fi
        done
    elif ! grep -q '^SET cameraFov ' "$cfg"; then
        printf 'SET cameraFov "100"\n' >>"$cfg"
    fi
}

if [[ ! -f "$PLAY_DIR/WTF/Config.wtf" && ! -L "$PLAY_DIR/WTF" ]]; then
    if [[ "$LOCAL_REALM" -eq 1 ]]; then
        printf 'SET locale "enUS"\nSET realmList "%s"\nSET hwDetect "0"\nSET readTOS "1"\nSET readEULA "1"\nSET cameraFov "100"\n' "$REALM_HOST" > "$PLAY_DIR/WTF/Config.wtf"
    else
        printf 'SET locale "enUS"\nSET realmList "%s"\nSET cameraFov "100"\n' "$REALM_HOST" > "$PLAY_DIR/WTF/Config.wtf"
    fi
else
    ensure_config_wtf
fi

cat <<EOF
Playtest tree: $PLAY_DIR  (gitignored)
Game data:     $GAME_SRC
Layout:        $(if [[ "$VANILLA" -eq 1 ]]; then echo "vanilla (MPQs + wowdecomp FrameXML patch only)"; else echo "full-install (symlinked Interface/Cache/Logs)"; fi)
Realm:         $REALM_HOST:3724

wowdecomp client ↔ AzerothCore only needs 3.3.5a protocol + matching build data.
Use --vanilla with a stock 3.3.5a Data tree; avoid Astral/wxl unless you pass --full-install on purpose.

After rebuilds, re-run this script.

  cd "$PLAY_DIR" && ./WowClient64
  Server: $ROOT/Tools/local-server/start.sh
EOF

mkdir -p "$PLAY_DIR/Interface/FrameXML"
cp -f "$ROOT/WoW/Interface/FrameXML/WowDecompVideoOptions.lua" "$PLAY_DIR/Interface/FrameXML/"

if [[ "$VANILLA" -eq 0 ]]; then
    mkdir -p "$PLAY_DIR/Interface/AddOns/WasmInspector"
    cp -f "$ROOT/Addons/Lua/WasmInspector/WasmInspector.toc" "$ROOT/Addons/Lua/WasmInspector/WasmInspector.lua" "$PLAY_DIR/Interface/AddOns/WasmInspector/"
fi
