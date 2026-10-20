#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SERVER_DIR="${WOWDECOMP_ACORE_DIR:-$ROOT/test/local-server/acore-docker}"
# shellcheck source=docker/cdn-env.sh
source "$ROOT/Tools/local-server/docker/cdn-env.sh"

if docker info >/dev/null 2>&1; then
    DOCKER=(docker)
elif sudo docker info >/dev/null 2>&1; then
    # ʕ •ᴥ•ʔ✿ sudo drops the CDN bind-mount paths, and compose then mounts the project dir onto a file ✿ ʕ •ᴥ•ʔ
    DOCKER=(sudo --preserve-env=WOWDECOMP_GAME_DATA,WOWDECOMP_CDN_NGINX_CONF,WOWDECOMP_CDN_SIGNATURE,DOCKER_CDN_EXTERNAL_PORT docker)
else
    echo "Docker backend selected but the daemon is not running." >&2
    echo "Install/start Docker, then re-run this script." >&2
    exit 1
fi

if ! "${DOCKER[@]}" compose version >/dev/null 2>&1; then
    echo "docker compose plugin is required (docker-compose v2)." >&2
    exit 1
fi

mkdir -p "$(dirname "$SERVER_DIR")"

if [[ ! -d "$SERVER_DIR/.git" ]]; then
    git clone --depth 1 https://github.com/azerothcore/acore-docker.git "$SERVER_DIR"
fi

cd "$SERVER_DIR"

if [[ ! -f .env ]]; then
    cp -n env/dist/.env.docker .env 2>/dev/null || true
    cat >> .env <<'EOF'

DOCKER_AUTH_EXTERNAL_PORT=3724
DOCKER_WORLD_EXTERNAL_PORT=8085
DOCKER_DB_EXTERNAL_PORT=63306
DOCKER_CDN_EXTERNAL_PORT=1119
EOF
fi

if [[ -f .env ]] && ! grep -q '^DOCKER_CDN_EXTERNAL_PORT=' .env; then
    echo 'DOCKER_CDN_EXTERNAL_PORT=1119' >> .env
fi

wowdecomp_prepare_cdn "$ROOT"
cdn_port=1119
if [[ -f .env ]] && grep -q '^DOCKER_CDN_EXTERNAL_PORT=' .env; then
    cdn_port="$(sed -n 's/^DOCKER_CDN_EXTERNAL_PORT=//p' .env | tail -1)"
fi
export DOCKER_CDN_EXTERNAL_PORT="$cdn_port"

compose() {
    local files=(-f "$SERVER_DIR/docker-compose.yml")
    # ʕ •ᴥ•ʔ✿ -f drops the automatic override. Without this, release recreates the stock worldserver and the bots stay offline. ✿ ʕ •ᴥ•ʔ
    if [[ -f "$SERVER_DIR/docker-compose.override.yml" ]]; then
        files+=(-f "$SERVER_DIR/docker-compose.override.yml")
    elif [[ "${WOWDECOMP_PLAYERBOTS:-}" == "1" ]]; then
        echo "WOWDECOMP_PLAYERBOTS=1 but $SERVER_DIR/docker-compose.override.yml is missing." >&2
        exit 1
    fi
    "${DOCKER[@]}" compose "${files[@]}" "${COMPOSE_EXTRA[@]}" "$@"
}

if ! "${DOCKER[@]}" image inspect "acore/ac-wotlk-worldserver:master" >/dev/null 2>&1; then
    echo "Pulling images (first run can take a while)..."
    compose pull
fi
compose up -d

db_password="password"
if [[ -f .env ]] && grep -q '^DOCKER_DB_ROOT_PASSWORD=' .env; then
    db_password="$(sed -n 's/^DOCKER_DB_ROOT_PASSWORD=//p' .env | tail -1)"
fi

mysql_exec() {
    compose exec -T ac-database mysql -uroot -p"$db_password" --batch --skip-column-names -e "$1"
}

echo "Waiting for the auth database..."
deadline=$((SECONDS + 900))
until mysql_exec "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='acore_auth' AND table_name='account'" 2>/dev/null | grep -qx 1; do
    if (( SECONDS > deadline )); then
        echo "AzerothCore database was not ready after 15 minutes." >&2
        compose logs --tail 40 ac-db-import ac-database >&2 || true
        exit 1
    fi
    sleep 3
done

mapfile -t reg < <(python3 "$ROOT/Tools/local-server/acore-account.py" a a)
salt="${reg[0]}"
verifier="${reg[1]}"
realm_host="${WOWDECOMP_REALM_HOST:-127.0.0.1}"
mysql_exec "INSERT INTO acore_auth.account (username, salt, verifier, expansion) VALUES ('A', UNHEX('${salt}'), UNHEX('${verifier}'), 2) ON DUPLICATE KEY UPDATE salt=VALUES(salt), verifier=VALUES(verifier), expansion=2;
INSERT INTO acore_auth.account_access (id, gmlevel, RealmID) SELECT id, 3, -1 FROM acore_auth.account WHERE username='A' ON DUPLICATE KEY UPDATE gmlevel=3;
UPDATE acore_auth.realmlist SET address='${realm_host}', localAddress='${realm_host}', port=8085 WHERE id=1;"

echo "Waiting for worldserver..."
deadline=$((SECONDS + 900))
# grep -q + pipefail treats SIGPIPE from compose as failure; read without -q
until compose logs --tail 200 ac-worldserver 2>/dev/null | grep -i "world initialized" >/dev/null; do
    if (( SECONDS > deadline )); then
        echo "worldserver did not finish starting." >&2
        compose logs --tail 40 ac-worldserver >&2 || true
        exit 1
    fi
    sleep 3
done

python3 "$ROOT/Tools/local-server/acore-account.py" a a --check 127.0.0.1:3724

cdn_line="CDN off (no Data folder)."
if [[ ${#COMPOSE_EXTRA[@]} -gt 0 ]]; then
    cdn_host="$(wowdecomp_cdn_host)"
    cdn_host="${cdn_host:-127.0.0.1}"
    sed "s|source=http://yourcdn.com/|source=http://${cdn_host}:${cdn_port}/|" \
        "$ROOT/ThirdParty/streamingclient/WoW.mfil.example" > "$SERVER_DIR/WoW.mfil"
    cdn_line="CDN http://${cdn_host}:${cdn_port}/  (WoW.mfil written to $SERVER_DIR/WoW.mfil)"
fi

cat <<EOF

AzerothCore (Docker, gitignored): $SERVER_DIR
Auth 127.0.0.1:3724  World 127.0.0.1:8085
$cdn_line
Account: a / a   gmlevel 3 (all realms)
Stop: WOWDECOMP_SERVER_BACKEND=docker $ROOT/Tools/local-server/stop.sh
EOF
