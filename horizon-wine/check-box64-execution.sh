#!/bin/sh
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
# Runs Box64 in switch-dev, under Docker, on an ARM64 machine or with qemu's
# binfmt handler registered for it (tonistiigi/binfmt --install arm64).
if ! command -v docker >/dev/null 2>&1; then
    echo "SKIP: needs docker (Box64 is built and run in the switch-dev image)" >&2
    exit 0
fi
case "$(uname -m)" in
    aarch64|arm64) ;;
    *)
        if [ ! -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ]; then
            echo "SKIP: needs an ARM64 machine or qemu's binfmt handler for arm64 (Box64 is an ARM64 build)" >&2
            exit 0
        fi
        ;;
esac
sh "$root/horizon-wine/tools/bootstrap-box64-core.sh"
# The Box64 checkout is the host's: git in the container must trust it.
docker run --rm --platform linux/arm64 -v "$root:/work" -w /work \
    -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e GIT_CONFIG_VALUE_0='*' \
    "$(cat "$root/horizon-wine/switch-dev.txt")" sh -ec '
    # Built in the container, and gone with it.
    cmake -S horizon-wine/tests/box64-core -B /tmp/box64-core -G Ninja
    cmake --build /tmp/box64-core -j 8
    ctest --test-dir /tmp/box64-core --output-on-failure
    cmake -S horizon-wine/tests/box64-core -B /tmp/box64-core-switch -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/work/horizon-wine/cmake/switch-devkitA64.cmake
    cmake --build /tmp/box64-core-switch -j 8
    '
