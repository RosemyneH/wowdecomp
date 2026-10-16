#!/bin/sh
set -eu
cd "$(dirname "$0")"
clang --target=wasm32-unknown-unknown -O2 -nostdlib -Wl,--no-entry -Wl,--export=wow_on_load -Wl,--export-memory -o addon.wasm hello.c
