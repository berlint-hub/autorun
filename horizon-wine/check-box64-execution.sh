#!/bin/sh
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
# Runs Box64 in switch-dev, under Docker.
if ! command -v docker >/dev/null 2>&1; then
    echo "SKIP: needs docker (Box64 is built and run in the switch-dev image)" >&2
    exit 0
fi
sh "$root/horizon-wine/tools/bootstrap-box64-core.sh"
docker run --rm --platform linux/arm64 -v "$root:/work" -w /work \
    "$(cat "$root/horizon-wine/switch-dev.txt")" sh -ec '
    # Built in the container, and gone with it.
    cmake -S horizon-wine/tests/box64-core -B /tmp/box64-core -G Ninja
    cmake --build /tmp/box64-core -j 8
    ctest --test-dir /tmp/box64-core --output-on-failure
    cmake -S horizon-wine/tests/box64-core -B /tmp/box64-core-switch -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/work/horizon-wine/cmake/switch-devkitA64.cmake
    cmake --build /tmp/box64-core-switch -j 8
    '
