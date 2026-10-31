#!/bin/sh
set -eu
export UBSAN_OPTIONS=halt_on_error=1
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cc="${CC:-$(command -v clang || command -v cc || command -v gcc)}"
[ -n "$cc" ] || { echo "No C compiler on PATH; set CC." >&2; exit 1; }
python3 "$root/horizon-wine/tests/check_swap_ui.py"
python3 "$root/horizon-wine/tests/check_swap_game.py"
python3 "$root/horizon-wine/tests/check_swap_threads.py"
python3 "$root/horizon-wine/tests/check_swap_native.py"
python3 "$root/horizon-wine/tests/check_commit_chunks.py"
build="$(mktemp -d "${TMPDIR:-/tmp}/autorun-swap.XXXXXX")"
trap 'rm -f "$build/pager" "$build/store" "$build/file" "$build/ipc" "$build/pool" "$build/index"; rmdir "$build"' EXIT HUP INT TERM
"$cc" -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    "$root/horizon-wine/tests/swap_index.c" -o "$build/index"
"$build/index"
"$cc" -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    "$root/horizon-wine/tests/horizon_pool.c" -o "$build/pool"
"$build/pool"
"$cc" -std=gnu11 -Wall -Wextra -Werror -pthread -fsanitize=address,undefined \
    "$root/horizon-wine/tests/swap_pager.c" "$root/horizon-wine/source/swap_pager.c" -o "$build/pager"
"$build/pager"
"$cc" -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    "$root/horizon-wine/tests/swap_store.c" "$root/horizon-wine/source/swap_store.c" \
    -Wl,--wrap=rename,--wrap=fsync -o "$build/store"
"$build/store"
"$cc" -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    "$root/horizon-wine/tests/swap_file.c" "$root/horizon-wine/source/swap_file.c" \
    "$root/horizon-wine/source/swap_store.c" -o "$build/file"
"$build/file"
if [ -n "${LIBNX_INCLUDE:-}" ]; then
    "$cc" -std=gnu11 -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
        -I"$LIBNX_INCLUDE" "$root/horizon-wine/tests/swap_ipc.c" -o "$build/ipc"
    "$build/ipc"
fi
