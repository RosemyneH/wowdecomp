#!/usr/bin/env bash
# ʕ •ᴥ•ʔ✿ Install root for MPQs. emma-laptop keeps the stock 3.3.5a tree in ~/Games/client ✿ ʕ •ᴥ•ʔ

wowdecomp_default_game_src() {
    if [[ -n "${WOWDECOMP_GAME_SRC:-}" ]]; then
        printf '%s\n' "$WOWDECOMP_GAME_SRC"
        return
    fi

    local host="${HOSTNAME:-}"
    if [[ -z "$host" ]]; then
        host="$(hostname -s 2>/dev/null || hostname 2>/dev/null || true)"
    fi

    if [[ "$host" == "emma-laptop" && -d "${HOME}/Games/client/Data" ]]; then
        printf '%s\n' "${HOME}/Games/client"
        return
    fi

    if [[ -d "${HOME}/Games/WoW335/Data" ]]; then
        printf '%s\n' "${HOME}/Games/WoW335"
        return
    fi

    if [[ -d "${HOME}/Games/client/Data" ]]; then
        printf '%s\n' "${HOME}/Games/client"
        return
    fi

    printf '%s\n' "${HOME}/Games/WoW335"
}
