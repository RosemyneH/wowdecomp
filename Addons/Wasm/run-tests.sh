#!/bin/sh
# ʕ •ᴥ•ʔ✿ Reproducible end-to-end check: Rust addons -> C++ host/tests -> benchmarks ✿ ʕ •ᴥ•ʔ
set -eu
cd "$(dirname "$0")/../.."
./Addons/Wasm/build.sh
sh Addons/Wasm/examples/c-hello/build.sh
cmake -S . -B build/wasm -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/wasm --target wasm_addon_host wasm_addon_tests wasm_addon_bench WowClient -j"$(nproc)"
./build/wasm/bin/wasm_addon_tests Addons/Wasm/dist
./build/wasm/bin/wasm_addon_host Addons/Wasm/dist/hello-world --event PLAYER_LOGIN --inspector
mkdir -p /tmp/wasm-c-hello && cp Addons/Wasm/examples/c-hello/addon.wasm Addons/Wasm/examples/c-hello/addon.toml /tmp/wasm-c-hello/
./build/wasm/bin/wasm_addon_host /tmp/wasm-c-hello
python3 Addons/Wasm/fuzz-manifest.py ./build/wasm/bin/wasm_addon_host
./build/wasm/bin/wasm_addon_bench Addons/Wasm/dist 200000
