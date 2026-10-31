#!/bin/sh
# Host tests of the actual audout backend and registry wire adapter.
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cc="${CC:-$(command -v clang || command -v cc || command -v gcc)}"
[ -n "$cc" ] || { echo "No C compiler on PATH; set CC." >&2; exit 1; }
build="$(mktemp -d "${TMPDIR:-/tmp}/wine-nx-audio.XXXXXX")"
trap 'rm -rf "$build"' EXIT HUP INT TERM
flags="-std=gnu11 -Wall -Wextra -Werror -D__WINESRC__ -DWINE_UNIX_LIB -D_WIN64 -fsanitize=address,undefined -fno-omit-frame-pointer"
"$cc" $flags -I"$root/include" "$root/horizon-wine/tests/registry_server.c" -o "$build/registry_server"
"$build/registry_server"
# The backend includes mmdevapi's PE headers, which the Wine build makes.
pe_include="${WINE_NX_PE_BUILD_DIR:-$root/horizon-wine/build-wine-amd64-pe}/include"
if [ ! -f "$pe_include/audioclient.h" ]; then
    echo "SKIP: the audout backend needs the PE build's headers; run sh build.sh first" >&2
    exit 0
fi
"$cc" $flags -I"$root/horizon-wine/tests/audio-shims" -I"$root/include" \
    -I"$pe_include" \
    "$root/horizon-wine/tests/audio_backend.c" -o "$build/audio"
"$build/audio"
