#!/bin/sh
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
build="${WINE_NX_TEST_BUILD_DIR:-$root/horizon-wine/toolchains/build-box64-amd64-tests}"
# Box64, the ARM64EC fixtures and the ARM64 host tests only run on an ARM64
# Linux host (or in switch-dev, which check-box64-execution.sh uses).
if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != aarch64 ]; then
    echo "SKIP: needs an ARM64 Linux host, this is $(uname -s) $(uname -m) (Box64 and the ARM64EC fixtures run there)" >&2
    exit 0
fi
sh "$root/horizon-wine/tools/bootstrap-box64-core.sh"
cmake -S "$root/horizon-wine/tests/box64-core" -B "$build" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build" -j "${WINE_NX_JOBS:-4}"
ctest --test-dir "$build" --output-on-failure
sh "$root/horizon-wine/tests/arm64ec-transitions/run.sh"
sh "$root/horizon-wine/tests/arm64ec-dispatchers/run.sh"
sh "$root/horizon-wine/tests/arm64ec-exception-frame/run.sh"
sh "$root/horizon-wine/tests/arm64ec-registry/run.sh"
sh "$root/horizon-wine/tests/arm64ec-startup/run.sh"
sh "$root/horizon-wine/tests/arm64ec-thread-init/run.sh"
python3 "$root/horizon-wine/tests/check_arm64ec_fpcsr.py"
python3 "$root/horizon-wine/tests/check_arm64ec_box64_unix.py"
python3 "$root/horizon-wine/tests/check_horizon_image_info.py"
python3 "$root/horizon-wine/tests/check_horizon_guest_reserve.py"
python3 "$root/horizon-wine/tests/check_horizon_address_space_limit.py"
python3 "$root/horizon-wine/tests/check_wow64_unix_tables.py"
python3 "$root/horizon-wine/tests/check_package_amd64.py"
python3 "$root/horizon-wine/tests/check_horizon_thread_fds.py"
python3 "$root/horizon-wine/tests/check_shared_cpu_context.py"
