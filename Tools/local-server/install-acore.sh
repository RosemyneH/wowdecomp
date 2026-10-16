#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
AC_ROOT="${WOWDECOMP_ACORE_SRC:-$ROOT/test/local-server/azerothcore-wotlk}"
PATCH="$ROOT/Tools/local-server/patches/mariadb-build.patch"
MARKER="$AC_ROOT/.wowdecomp-mariadb-patch"

if [[ ! -d "$AC_ROOT/.git" ]]; then
    mkdir -p "$(dirname "$AC_ROOT")"
    git clone --depth 1 https://github.com/azerothcore/azerothcore-wotlk.git "$AC_ROOT"
fi

if [[ ! -f "$MARKER" ]]; then
    patch -d "$AC_ROOT" -p1 -N < "$PATCH"
    touch "$MARKER"
fi

cd "$AC_ROOT"
./acore.sh compiler all

BIN="$AC_ROOT/env/dist/bin"
if [[ ! -x "$BIN/worldserver" ]]; then
    echo "Build failed — see $ROOT/test/local-server/acore-build.log" >&2
    exit 1
fi

echo ""
echo "Built: $BIN"
"$ROOT/Tools/local-server/bootstrap-acore-db.sh"
cd "$AC_ROOT" && ./acore.sh client-data

echo ""
echo "Start: WOWDECOMP_SERVER_BACKEND=native $ROOT/Tools/local-server/start.sh"
echo "First login: attach worldserver and run: account create wowdecomp wowdecomp"
