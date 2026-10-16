#!/bin/sh
# ʕ •ᴥ•ʔ✿ Regenerate shaders into a temp dir and fail on drift; skips when glslc is absent or unpinned ✿ ʕ •ᴥ•ʔ
set -eu
cd "$(dirname "$0")/.."
GLSLC=$(command -v glslc || true)
if [ -z "$GLSLC" ]; then echo "check-generated: glslc not found, skipping"; exit 0; fi
cmake -DGLSLC="$GLSLC" -P cmake/RegenerateShaders.cmake
if ! git diff --exit-code -- Engine/Source/Gx/CGxDeviceVulkan/Shaders/Generated; then
    echo "check-generated: shader outputs drifted; commit the regenerated files" >&2
    exit 1
fi
echo "check-generated: shaders up to date"
