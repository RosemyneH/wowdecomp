#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
AC_ROOT="${WOWDECOMP_ACORE_SRC:-$ROOT/test/local-server/azerothcore-wotlk}"
SQL="$AC_ROOT/data/sql/create/create_mysql.sql"

if ! sudo mysql -u root -e "SELECT 1" >/dev/null 2>&1; then
    echo "Need sudo access to MariaDB (unix_socket root on Arch)." >&2
    exit 1
fi

if ! sudo mysql -u root -e "SHOW DATABASES LIKE 'acore_auth';" | grep -q acore_auth; then
    echo "Creating acore databases and user..."
    sudo mysql -u root < "$SQL"
else
    echo "acore_* databases already exist."
fi

mariadb -u acore -pacore -e "UPDATE acore_auth.realmlist SET flag = 0 WHERE flag & 3;" 2>/dev/null || true

echo "DB user: acore / acore (localhost)"
echo "Worldserver will apply SQL updates on first start."
