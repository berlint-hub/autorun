# Building an Autorun card that carries the tuned FEX

This is the note to hand to whoever builds the release card. It is three
tasks, and all three are needed: the runtime and the translator are two
separate builds, and a card that has only one of them is a card that behaves
like the last one did.

Nothing on this branch compiles FEX — that stays the FEX work's — but the
tree the FEX work builds from is pinned, and the patch that makes it usable on
the console is in this directory, so a builder no longer needs the FEX work's
working branch to reach the same source.

## 1. Take the runtime from this branch

Branch `arena/97e282a9-autorun`, at the commit the card is cut from. It
contains everything the console needs for the picture: the display handover
between the compositor and the program's own OpenGL surface (the window is
resized only after the buffers in the way are released, and a window that
still refuses says so), the frame instrumentation that names a swap stuck in
the driver, `logs/driver.txt` for the graphics driver's own words, the memory
report the allocator can back, and the thread-slot release. No patches, no
local edits.

A card is only as good as the runtime on it: `[BUILD]` in
`logs/autorun_runtime.log` says which build ran, and `[LOG] this run is kept
in …` says which file to read when something looks wrong.

## 2. Apply `fex-2610-horizon-memory.patch` to FEX `6ec29a2e`

```sh
git clone https://github.com/berlint-hub/FEX
cd FEX
# the Horizon port: FEX-2610
git checkout 6ec29a2e70fcec313b1d85a1c806746dafc4684b
git apply --check <autorun>/horizon-wine/fex/fex-2610-horizon-memory.patch
git apply         <autorun>/horizon-wine/fex/fex-2610-horizon-memory.patch
git submodule update --init --depth=1 \
    External/fmt External/xxhash External/range-v3 External/unordered_dense \
    External/rpmalloc Source/Common/cpp-optparse
```

The patch is made against exactly that commit; it is a plain unified diff
over 31 files and it applies with `git apply --check` on a clean tree of it.

What it does, in one line each:

- **The memory layer, which is what the port was missing.**
  `Source/Windows/Common/Horizon.cpp` (new) answers the JIT's requests
  through the runtime's unixlib: `WINE_NX_FEX_ALLOC` for a code mapping and
  its writable alias, `WINE_NX_FEX_FLUSH` for the real I-cache maintenance,
  `WINE_NX_FEX_PROTECT_CODE` for W^X, `WINE_NX_FEX_QUERY` and
  `WINE_NX_FEX_ATTACH_THREAD` for the exception stack. FEX-2610 shipped
  stubs here — a pointer handed back unchanged and `__builtin___clear_cache`,
  which does nothing on Windows — and a module built from it dies with
  `STATUS_ILLEGAL_INSTRUCTION` at its first translation.
- **The CRT heap over `WINE_NX_FEX_HEAP_ALLOC`**
  (`Source/Windows/Common/CRT/HorizonHeap.{h,cpp}`, and the
  `FEX_HORIZON` branch in `FEXCore/Source/Utils/AllocatorHooks.cpp`), so a
  module does not bring an allocator of its own onto a console with a 513 MB
  pool to share. `CRT/CMakeLists.txt` builds one of `Alloc.cpp` and
  `HorizonHeap.cpp`, not both: they define the same names.
- **The code store grows in chunks** (`SharedCodeBufferManager`,
  `code_storage.h`): 16 MB to start, up to a policy ceiling, halving a
  request that cannot be met instead of dying on it, and the guard page is
  gone because the chunks are what bounds it. `CPUBackend::IsAddressInCodeBuffer`
  asks the store instead of comparing against one contiguous range.
- **The L2 lookup cache is sized by the policy** (`cache_policy.h`,
  `LookupCache`, `Dispatcher`): the hash and the mask match on both sides,
  `VirtualMemSize` is not a single power of two, and `SetCodeCacheLimit` is
  set from `CodeCacheLimit()` at context creation.
- **The ABI headers are this repository's**, byte for byte
  (`Source/Windows/include/{unixlib,jit_registry,code_storage,cache_policy}.h`
  are copies of `horizon-wine/fex/`). That is the point of the copies: the
  runtime implements opcodes 7–15 of that header and nothing else, and a
  module built against a different one does not find them.
- **Startup and threads**: `HorizonInitialize()` in `BTCpuProcessInit` and
  `HorizonAttachThread()` in the WOW64/ARM64EC thread init, plus the
  read-file halves (`PrepareNativeWrite` before, `InvalidateAlignedInterval`
  after) that keep self-modifying code honest where there are no signals.
- **`CMakeLists.txt`**: one `FEX_HORIZON` option (there were two), usable
  without `FEX_HORIZON_ABI_DIR` (the bundled headers are the same files), and
  no `FATAL_ERROR` that made the fallback unreachable.

## 3. Build the two modules for Tegra X1, and pack them with the runtime

```sh
cmake -S . -B build-arm64ec -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=Data/CMake/toolchain_mingw.cmake \
  -DMINGW_TRIPLE=arm64ec-w64-mingw32 -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_LTO=OFF -DBUILD_TESTING=OFF -DBUILD_FEXCONFIG=OFF \
  -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF -DENABLE_OFFLINE_TELEMETRY=OFF \
  -DFEX_HORIZON=ON -DFEX_HORIZON_ABI_DIR=<autorun>/horizon-wine/fex \
  -DFEX_TUNE_TEGRA_X1=ON
cmake --build build-arm64ec --target arm64ecfex -j"$(nproc)"

cmake -S . -B build-wow64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=Data/CMake/toolchain_mingw.cmake \
  -DMINGW_TRIPLE=aarch64-w64-mingw32 -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_LTO=OFF -DBUILD_TESTING=OFF -DBUILD_FEXCONFIG=OFF \
  -DENABLE_JEMALLOC_GLIBC_ALLOC=OFF -DENABLE_OFFLINE_TELEMETRY=OFF \
  -DFEX_HORIZON=ON -DFEX_HORIZON_ABI_DIR=<autorun>/horizon-wine/fex \
  -DFEX_TUNE_TEGRA_X1=ON
cmake --build build-wow64 --target wow64fex -j"$(nproc)"
```

The two differ only in `MINGW_TRIPLE` and the build directory; nothing
else in them is optional, and both need the patch and the submodules from
step 2. `FEX_HORIZON_ABI_DIR` may be dropped -- the headers inside the tree
are those files -- but naming this repository's directory is what keeps a
module built against the same opcodes the runtime implements.

`FEX_TUNE_TEGRA_X1=ON` sets `TUNE_CPU=cortex-a57` and
`TUNE_ARCH=armv8-a+crc` and defines `FEX_TUNE_TEGRA_X1`, which is what the
hand-written Tegra X1 patches in that tree key off (`CNTVCT_EL0` instead of
the counter the console does not expose the same way, the 32-bit `DIV`/`IDIV`
fast path, the suspend check on a backward conditional jump). The arms are
`arm64ecfex` and `wow64fex`; the modules are `build/Bin/libarm64ecfex.dll`
and `build-wow64/Bin/libwow64fex.dll`.

They go onto the card as the `-opt` pair, beside the stock modules, which do
not change: `libarm64ecfex-opt.dll` and `libwow64fex-opt.dll`, with the
payload's `fex-opt-manifest.json` recording `"tune": "cortex-a57"`,
`"revision": "6ec29a2e"` and the patch's name. The payload and the archive
are built by the FEX work's `build-fex.sh` and `fex_payload.py`
(`WINE_NX_FEX_SUFFIX=-opt`, `WINE_NX_FEX_PATCH=fex-2610-horizon-memory.patch`),
or by hand to the same shape; either way the runtime finds a variant by its
stem, so the `-opt` names are all it needs.

## What is checked, and what is not

- **Checked**: the runtime builds and its host tests pass (CI on
  `arena/97e282a9-autorun`); the patch applies to `6ec29a2e` on a clean tree;
  the ABI headers it carries are identical to this repository's; FEXCore
  itself builds for AArch64 from that source.
- **Not checked**: the two Windows modules have not been built here, and no
  card carrying them has been booted. The patch ports a memory layer that is
  proven in FEX-2609 (`horizon.patch`) onto FEX-2610's source, and FEX-2610
  moved under it — `CodeBuffer` grew a `ShouldBeNamed` argument, the CRT
  allocator selection changed, the ARM64EC thread entry is a different
  function. Each of those was placed by reading the target tree, not by
  compiling it.
- So: **do not promise the card will carry a faster translator until a card
  with these modules has started a 32-bit program and its log shows `[CPU]
  FEX (libwow64fex-opt.dll)`**. Until then the honest sentence is "the
  runtime is fixed; the tuned translator needs its own build and its own
  run".

## When it does not start

- `[CPU] FEX (libwow64fex.dll)` instead of the `-opt` name: the variant
  module is not on the card or is not the name the settings ask for. The
  runtime falls back on purpose.
- `STATUS_DLL_INIT_FAILED` right after `wow64win` attached, before FEX reads
  its config: the module did not pair with its unixlib (see
  `startup-variant-unixlib.md`; the stem-based pairing is in this branch).
- `STATUS_ILLEGAL_INSTRUCTION` at the first translations: the I-cache was
  never flushed, which is what a module built without the memory layer looks
  like — check that the patch was applied and that `HorizonInitialize()` is
  in the log's path (`Horizon` failing to start is `ERROR_AND_DIE_FMT` in
  the module, so a module built with the patch but failing the query dies at
  load rather than running blind).
- A card that starts but shows nothing: that is the runtime's handover, not
  the translator. `[NXGL] swap #1 began…` / `came back…`, the window line in
  the stall report and `logs/driver.txt` say where it stopped.
