#!/usr/bin/env bash

# ʕ •ᴥ•ʔ✿ Fills COMPOSE_EXTRA and the bind-mount env the streaming compose file reads ✿ ʕ •ᴥ•ʔ
wowdecomp_prepare_cdn() {
    local root="$1"
    local game_src="${WOWDECOMP_GAME_SRC:-$HOME/Games/WoW335}"
    local data="$game_src/Data"

    COMPOSE_EXTRA=()
    if [[ ! -d "$data" ]]; then
        echo "No 3.3.5a Data at $data — CDN stays off. Set WOWDECOMP_GAME_SRC." >&2
        return 0
    fi

    export WOWDECOMP_GAME_DATA
    WOWDECOMP_GAME_DATA="$(readlink -f "$data")"
    export WOWDECOMP_CDN_NGINX_CONF="$root/Tools/local-server/docker/nginx.conf"
    export WOWDECOMP_CDN_SIGNATURE="$root/Tools/local-server/docker/signaturefile"

    if [[ ! -s "$WOWDECOMP_CDN_SIGNATURE" ]] && [[ -f "$data/patch-3.MPQ" ]]; then
        local patch3="$data/patch-3.MPQ"
        if command -v uv >/dev/null 2>&1; then
            uv run --with mpyq python -c "import mpyq; open('$WOWDECOMP_CDN_SIGNATURE','wb').write(mpyq.MPQArchive('$patch3').read_file('signaturefile'))" 2>/dev/null \
                || echo "Could not extract signaturefile from $patch3 (uv/mpyq)." >&2
        else
            echo "CDN signaturefile is empty; install uv or copy signaturefile from patch-3.MPQ." >&2
        fi
    fi

    COMPOSE_EXTRA=(-f "$root/Tools/local-server/docker/streaming.yml")
}

wowdecomp_cdn_host() {
    ip -4 route get 1.1.1.1 2>/dev/null | awk '{for (i = 1; i <= NF; i++) if ($i == "src") { print $(i + 1); exit }}'
}
