#!/usr/bin/env bash
# Builds the standalone FPS payload. The ONLY deliverable is
#   standalone_fps/build/bin/fps_counter.elf
# (the overlay module is embedded in it). Intermediates, including the overlay
# shellui.elf, are under standalone_fps/build/internal/ and are not payloads.
set -euo pipefail
: "${PS5_PAYLOAD_SDK:?export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/build"
OUT="$BUILD/bin/fps_counter.elf"

# Drop leftovers from older layouts so nothing but fps_counter.elf can be picked up.
rm -f "$BUILD/bin/shellui.elf" "$OUT"

"$PS5_PAYLOAD_SDK/bin/prospero-cmake" -S "$HERE" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DPS5_PAYLOAD_SDK="$PS5_PAYLOAD_SDK" \
  -DCMAKE_MESSAGE_LOG_LEVEL=WARNING >/dev/null
ninja -C "$BUILD" fps_counter

[ -f "$OUT" ] || { echo "build failed: $OUT was not produced" >&2; exit 1; }
n="$(find "$BUILD/bin" -type f | wc -l)"
[ "$n" -eq 1 ] || { echo "unexpected extra files in $BUILD/bin" >&2; exit 1; }

echo
echo "Payload (load only this file): $OUT"
