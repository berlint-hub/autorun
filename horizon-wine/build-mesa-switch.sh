#!/bin/sh
# mesa-switch (https://github.com/berlint-hub/mesa-switch, a fork of
# https://github.com/danfromtico/mesa-switch): EGL, OpenGL through Gallium
# nvc0 and NVK Vulkan from one Meson build, newer than the switch-dev image's.
#
#   sh horizon-wine/build-mesa-switch.sh
#
# The image named in switch-dev.txt has mesa-switch in portlibs at the commit
# its dev.autorun.mesa-switch label names (d4a00ea, Mesa 26.2.3). This builds
# the commit pinned here in that same image, with the image's own recipe
# (/usr/local/share/switch-dev/mesa-build.sh), so the options and the
# toolchain are the image's, and stages it in vendor/mesa-switch/pkg:
#
#   lib/      the archives the runtime links (CMakeLists.txt,
#             WINE_NX_MESA_SWITCH_DIR points here)
#   include/  the headers the build installed that differ from the image's;
#             the runtime is compiled against them first (../include)
#
# build.sh links every runtime with it. A package already there for this
# commit and image is kept: the first build takes a while.
#
# What the pinned commit has that the image's does not: Mesa 26.2.4, the
# GPFIFO submission capacity accounting, NVK's depth-stencil memory bindings,
# and nouveau_horizon_runtime_shutdown closing the devices' cached buffers,
# without which the homebrew menu could not take the heap back (0xD401) after
# a program that leaked a VkDeviceMemory.
set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
hw="$root/horizon-wine"
repository=https://github.com/berlint-hub/mesa-switch
commit=0ff52079ca9e9e837ed27eb95a8d239f439a1864
src="$hw/vendor/mesa-switch/src"
pkg="$hw/vendor/mesa-switch/pkg"
image="${WINE_NX_DEVKIT_IMAGE:-$(cat "$hw/switch-dev.txt")}"
# What the runtime links (CMakeLists.txt), and the rest of what Mesa installs.
libs="EGL GL glapi vulkan mesa_util_c11 blake3 mesa_util mesa_util_simd xmlconfig GLESv1_CM GLESv2"
stamp="$commit $image"

if [ -f "$pkg/lib/libvulkan.a" ] && [ "$(cat "$pkg/commit" 2>/dev/null)" = "$stamp" ]; then
    echo "mesa-switch $commit: package already built in $pkg"
    exit 0
fi
command -v docker >/dev/null || { echo "Missing docker." >&2; exit 1; }

if [ ! -d "$src/.git" ]; then
    mkdir -p "$src"
    git init -q "$src"
    git -C "$src" remote add origin "$repository"
fi
if [ "$(git -C "$src" rev-parse HEAD 2>/dev/null)" != "$commit" ]; then
    git -C "$src" fetch -q --depth 1 origin "$commit"
fi
# From the commit's own files each time. The Rust crates Meson downloaded
# (subprojects/) are kept: they are named by version and checked by hash.
git -C "$src" checkout -q --force "$commit"

rm -rf "$pkg.new"
mkdir -p "$pkg.new"
# The recipe installs into the container's portlibs; what it installed is
# taken from there: the archives by name, and the headers whose bytes are new.
docker run --rm -v "$src:/mesa" -v "$pkg.new:/out" -w /mesa "$image" bash -ec '
    portlibs=/opt/devkitpro/portlibs/switch
    # What the container writes is given back to the caller, also on failure.
    trap "chown -R \"$1\" /out /mesa" EXIT
    (cd $portlibs/include && find . -type f -exec md5sum {} + | sort) > /tmp/include-before
    bash /usr/local/share/switch-dev/mesa-build.sh
    (cd $portlibs/include && find . -type f -exec md5sum {} + | sort) > /tmp/include-after
    mkdir -p /out/lib /out/include
    for lib in $2; do
        if [ -f $portlibs/lib/lib$lib.a ]; then cp -p $portlibs/lib/lib$lib.a /out/lib/; fi
    done
    comm -13 /tmp/include-before /tmp/include-after | sed "s/^[0-9a-f]*  //" > /tmp/include-changed
    tar -C $portlibs/include -cf - -T /tmp/include-changed | tar -C /out/include -xf -
    cp -p $portlibs/share/licenses/mesa-switch/license.rst /out/
    ' sh "$(id -u):$(id -g)" "$libs"
for lib in EGL GL glapi vulkan mesa_util_c11 blake3 mesa_util mesa_util_simd xmlconfig; do
    [ -f "$pkg.new/lib/lib$lib.a" ] || { echo "mesa-switch did not install lib$lib.a" >&2; exit 1; }
done
echo "$stamp" > "$pkg.new/commit"
rm -rf "$pkg"
mv "$pkg.new" "$pkg"
echo "mesa-switch $commit:"
ls -l "$pkg/lib"
(cd "$pkg/include" && find . -type f | sort | head -n 40)
