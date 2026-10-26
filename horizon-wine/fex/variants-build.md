# FEX variants: the stock release and a tuned build, side by side

The DLL payload can carry two FEX CPU-module builds, so the two can be
compared on the Switch without rebuilding or reinstalling:

- **stock** — `libwow64fex.dll` and `libarm64ecfex.dll`, from the pinned
  `FEX-Emu/FEX@395b132f` (FEX-2609) with `horizon.patch` applied on top,
  exactly as the release pipeline has always built them;
- **opt** — `libwow64fex-opt.dll` and `libarm64ecfex-opt.dll`, the same
  pinned `FEX-Emu/FEX@395b132f` with `horizon.patch`, built for the console's
  CPU: `WINE_NX_FEX_TUNE_TEGRA_X1=ON` passes `-DTUNE_CPU=cortex-a57` (and
  `-DFEX_TUNE_TEGRA_X1=ON`, which the pinned tree does not know and ignores;
  a tree that does know it strips the ARMv8.1+ JIT branches). The payload
  records what it was tuned for — `"tune": "cortex-a57"` in
  `fex-opt-manifest.json` — and `build-dlls.py` stops a build whose manifest
  disagrees with the recipe, so a variant cannot quietly become a copy of the
  stock modules under another name.

The first tuned variant died at the first guest init with
`STATUS_DLL_INIT_FAILED`, right after `wow64win` attached and before FEX read
its config, which read as the tuning's fault. It was not: ntdll paired the CPU
module by its exact `BaseDllName`, so `libwow64fex-opt.dll` found no unixlib,
and a byte-twin build with the tuning off died the same way. The stem-based
pairing (`dlls/ntdll/unix/horizon_unixlib_names.h`, see
`startup-variant-unixlib.md`) is what fixed it, and the tuning is back on.

The twin is still the control when a startup failure has to be placed: run the
**FEX variant** workflow with `tune: off` (or build with
`WINE_NX_FEX_TUNE_TEGRA_X1=OFF` locally) and the modules take the stock flags
under the `-opt` names. A control build is never published as the `fex-opt`
release; the release is what the main Build fetches into every SD archive.

Both share Autorun's ABI headers in `horizon-wine/fex/` and are validated the
same way. The point of the pair is an honest A/B on hardware: the tuning is
expected to be a small, safe win. The larger step, a newer FEX translator,
is held back until its Horizon layer is complete (see below).

## The build side

The changes live in the `autorun-horizon-dlls` tools and are kept here as
`dlls-fex-variants.patch`, made against the `horizon-dlls` submodule pin,
until they land upstream:

- `tools/build-fex.sh` takes `WINE_NX_FEX_REMOTE`, `_REVISION`, `_PATCH`
  (empty for none), `_TUNE_TEGRA_X1`, `_VERSION` and `_SUFFIX`, and gives the
  modules and their payload directory the suffix when one is set — the recipe
  leaves the source at the pinned release and the patch at `horizon.patch`,
  and sets `_TUNE_TEGRA_X1=ON` (`-DTUNE_CPU=cortex-a57`, plus
  `-DFEX_TUNE_TEGRA_X1` for a tree that knows that option);
- `tools/fex_payload.py` takes `--suffix`, `--version`, `--revision` and
  `--patch`, so a variant's manifest honestly records its own source;
- `tools/build-dlls.py` builds the variant as the `fex-opt` entry, from the
  recipe in `FEX_VARIANT`, beside the pinned `fex` one.

To land it: apply the patch on top of the `horizon-dlls` submodule pin
(`git -C horizon-dlls am ../../horizon-wine/fex/dlls-fex-variants.patch`),
push, and update the pin here; or open it as a pull request against
`autorunhq/autorun-horizon-dlls`. `build.sh --dlls` then produces both module
pairs in `switch/wine/drive_c/windows/system32/`.

`build-fex.sh` applies one patch from `horizon-wine/fex/`, so a source tree
that sets `TUNE_ARCH` to `armv8-a+crc` (the FEX-2610 port, which also knows
`FEX_TUNE_TEGRA_X1`) needs `tegra-x1-arch-crc.patch` as well, or CMake spells
the arch `armv8-a+crc+crc` and the compiler rejects it. The pinned FEX-2609
takes `TUNE_CPU=cortex-a57` alone and needs none of it; the patch is kept for
the day the variant's source changes.

## Who builds what

The FEX tree owns its build. Whoever changes it publishes the modules as the
**`fex-opt` release** on `berlint-hub/FEX`: `fex-opt-modules.zip` (laid out as
`switch/wine/`, the two modules and `fex-opt-manifest.json`) and `SHA256SUMS`.
`fex-variant-release.yml` in this folder is that workflow, ready to drop into
`.github/workflows/` there.

The `FEX variant` workflow (`.github/workflows/fex-variant.yml`) takes that
release first, checks it against its checksums, and uploads it as the SD
overlay artifact; while no release is published, it builds the same modules
from the same sources itself and publishes them as this repository's own
`fex-opt` release, the publisher of last resort. The moment the FEX tree's
release appears, the download takes over without any change here.

The main build takes the modules the same way (the `Fetch the tuned FEX` step
of `build.yml`, into `package-autorun.py --extra`): whichever `fex-opt`
release exists is checked and staged with `fex-variant.txt` set to `opt`, so
the SD archive runs the tuned variant out of the box and returns to the
pinned ones when that file is deleted. `verify-package.py` knows the two
variant module names as the one thing in the package not from the DLL
repository.

## Why the FEX-2610 port is not the variant yet

`berlint-hub/FEX@b8674872` (the `arena/799d045b-fex` branch) is FEX-2610
with a Horizon port whose **memory layer is still fallback stubs**, and the
stubs fault on hardware: `Source/Windows/Common/Horizon.cpp` only answers
address-space questions, `CRT/HorizonHeap.cpp` returns the pointer unchanged
from `HorizonWritableAddress` (no writable alias for the W^X code buffers)
and flushes with `__builtin___clear_cache`, which does nothing on Windows —
so translated code is executed without the I-cache ever being synchronized
and dies with `STATUS_ILLEGAL_INSTRUCTION` at the first translations
(reproduced on the Switch, `libwow64fex-opt.dll` from that source).

The working layer is `horizon.patch`'s `Source/Windows/Common/Horizon.cpp`:
the JIT buffers are allocated through the runtime's unixlib
(`WINE_NX_FEX_ALLOC`, a readable-executable mapping and its writable alias
published to `jit_registry.h`'s registry), writes go through
`HorizonWritableAddress` to the alias, and `HorizonFlushICache` calls
`WINE_NX_FEX_FLUSH`, the runtime's real cache maintenance. The compile fixes
that port still needs are kept in `tegra-x1-fixes.patch` (declarations in
`Horizon.h` instead of inline definitions, the `SupportsAVX256` line
FEX-2610 dropped, `CodeBuffer::Contains`); what it still owes is that
allocation/flush/protect layer — `HorizonAllocateJit`, `HorizonFreeJit`,
`HorizonProtectCode` (the SMC hooks `InvalidationTracker` shows in
`horizon.patch`), and the CRT heap over `WINE_NX_FEX_ALLOC` — wired from
`SharedCodeBufferManager` and the dispatcher. When a FEX tree carries all of
that, it can become the `fex-opt` source the way `fex-variant-release.yml`
describes.

## A/B on the Switch

The per-game choice is in Game Settings → Emulation → **CPU translator**:
Box64, FEX (the stock modules), or FEX optimized (the `-opt` ones). A game
without its own choice follows `sdmc:/switch/wine/fex-variant.txt`: a suffix
(`opt`) or absent/empty for stock. The runtime loads the variant's
WoW64/ARM64EC CPU module when one is named and present; if the module is
missing it falls back to the stock one. The start log's `[CPU]` line says
which one runs:

```
[CPU] FEX (libwow64fex-opt.dll)     <- opt variant
[CPU] FEX (libwow64fex.dll)         <- stock
```

Pick a translator per game, or delete the file to return the unpinned ones
to the stock modules; no reinstalling is needed.

Before believing a run, check which build the card actually carries:
`switch/wine/fex-opt-manifest.json` says what the modules were tuned for
(`"tune": "cortex-a57"`, or `null` for the control), and the `[CPU]` line says
which file the runtime loaded. A tuned build that starts only as the twin is
not a tuning result, it is the unixlib pairing again.
