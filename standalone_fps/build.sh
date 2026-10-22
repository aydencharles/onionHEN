#!/usr/bin/env bash
set -euo pipefail
: "${PS5_PAYLOAD_SDK:?export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$PS5_PAYLOAD_SDK/bin/prospero-cmake" -S "$HERE" -B "$HERE/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DPS5_PAYLOAD_SDK="$PS5_PAYLOAD_SDK"
ninja -C "$HERE/build"
echo "=> $HERE/build/bin/fps_counter.elf (overlay is embedded)"
