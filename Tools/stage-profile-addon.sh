#!/usr/bin/env bash
# ʕ •ᴥ•ʔ✿ Copy WowdecompProfile into a play scope (Interface/AddOns) ✿ ʕ •ᴥ•ʔ
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLAY="${1:-${WOWDECOMP_PLAY_DIR:-$ROOT/test/dev-335}}"
mkdir -p "$PLAY/Interface/AddOns/WowdecompProfile"
cp -f "$ROOT/Addons/WowdecompProfile/WowdecompProfile.toc" \
    "$ROOT/Addons/WowdecompProfile/WowdecompProfile.lua" \
    "$PLAY/Interface/AddOns/WowdecompProfile/"
