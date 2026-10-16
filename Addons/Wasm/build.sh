#!/bin/sh
# ʕ •ᴥ•ʔ✿ Builds every Rust example/fixture and stages portable packages in Addons/Wasm/dist ✿ ʕ •ᴥ•ʔ
set -eu
cd "$(dirname "$0")"
DIST="$PWD/dist"
rm -rf "$DIST"
build() {
    crate="$1"; name="$2"; toml="$3"
    cargo build --locked --manifest-path "$crate/Cargo.toml" --target wasm32-unknown-unknown --release --quiet
    wasm=$(ls "$crate"/target/wasm32-unknown-unknown/release/*.wasm | head -1)
    mkdir -p "$DIST/$name"
    cp "$wasm" "$DIST/$name/addon.wasm"
    cp "$toml" "$DIST/$name/addon.toml"
    echo "built $name"
}
build examples/hello-world hello-world examples/hello-world/addon.toml
build examples/inventory-watcher inventory-watcher examples/inventory-watcher/addon.toml
build examples/ui-demo ui-demo examples/ui-demo/addon.toml
build fixtures/ping ping fixtures/ping/addon.toml
build fixtures/loop loop fixtures/loop/addon.toml
build fixtures/memory-bomb memory-bomb fixtures/memory-bomb/addon.toml
build fixtures/bad-pointer bad-pointer fixtures/bad-pointer/addon.toml
build fixtures/unauthorized unauthorized fixtures/unauthorized/addon.toml
build fixtures/unknown-import unknown-import fixtures/unknown-import/addon.toml
build fixtures/ui-creator ui-creator fixtures/ui-creator/addon.toml
build fixtures/storage-test storage-test fixtures/storage-test/addon.toml
build fixtures/payload-test payload-test fixtures/payload-test/addon.toml
build fixtures/unit-auction-test unit-auction-test fixtures/unit-auction-test/addon.toml
build fixtures/bench-scale bench-scale fixtures/bench-scale/addon.toml
build fixtures/wrong-signature wrong-signature fixtures/wrong-signature/addon.toml
build fixtures/host-edge host-edge fixtures/host-edge/addon.toml
build fixtures/confirm-test confirm-test fixtures/confirm-test/addon.toml
