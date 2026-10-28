# FEX variants: the stock release and a tuned build, side by side

The DLL payload can carry two FEX CPU-module builds, so the two can be
compared on the Switch without rebuilding or reinstalling:

- **stock** — `libwow64fex.dll` and `libarm64ecfex.dll`, from the pinned
  `FEX-Emu/FEX@395b132f` (FEX-2609) with `horizon.patch` applied on top,
  exactly as the release pipeline has always built them;
- **opt** — `libwow64fex-opt.dll` and `libarm64ecfex-opt.dll`, the tuned
  build of the same translator (the pinned `FEX-Emu/FEX@395b132f` with
  `horizon.patch`, compiled for the console's Cortex-A57 and whatever else the
  FEX work tunes next). Its payload records what it was tuned for —
  `"tune"` in `fex-opt-manifest.json` — so a card can say which build it
  carries.

**This repository does not build FEX.** The tuning, the source tree, the
compile and the `fex-opt` release are the FEX work's
(`arena/21bcd47b-autorun`, publishing from `berlint-hub/FEX`); this branch
only takes that release, checks it against its checksums and ships it — into
the SD archive (`build.yml`) and as a small overlay for the A/B
(`fex-variant.yml`). Nothing here compiles, tunes or publishes a module, so
the modules a card runs have one author.

The first tuned variant died at the first guest init with
`STATUS_DLL_INIT_FAILED`, right after `wow64win` attached and before FEX read
its config, which read as the tuning's fault. It was not: ntdll paired the CPU
module by its exact `BaseDllName`, so `libwow64fex-opt.dll` found no unixlib,
and a byte-twin build with the tuning off died the same way. The stem-based
pairing (`dlls/ntdll/unix/horizon_unixlib_names.h`, see
`startup-variant-unixlib.md`) is what fixed it, and the tuning is back on.

The twin is still the control when a startup failure has to be placed: the FEX
work builds it with `WINE_NX_FEX_TUNE_TEGRA_X1=OFF` (the stock flags under the
`-opt` names) and publishes it the same way; a run that pairs the twin's
`fex-opt` with this runtime then tells a tuning problem from a variant-path
problem. Ask for it there rather than building one here.

Both share Autorun's ABI headers in `horizon-wine/fex/` and are validated the
same way. The point of the pair is an honest A/B on hardware: the tuning is
expected to be a small, safe win. The larger step, a newer FEX translator,
is held back until its Horizon layer is complete (see below).

## The build side (the FEX work's)

Recorded here because this branch reads the result, not because it runs any of
it. The changes live in the `autorun-horizon-dlls` tools and are kept as
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

**The FEX work builds.** It is `arena/21bcd47b-autorun` and the tree it
publishes from, `berlint-hub/FEX`; the modules come out of there as the
**`fex-opt` release**: `fex-opt-modules.zip` (laid out as `switch/wine/`, the
two modules and `fex-opt-manifest.json`) and `SHA256SUMS`.
`fex-variant-release.yml` in this folder is the workflow that publishes it.

**This branch ships.** Two places take that release and nothing else:

- the `FEX variant` workflow (`.github/workflows/fex-variant.yml`) downloads
  it, checks the checksums, and packs it as the SD overlay artifact — a drop
  that needs no rebuild to try on the console. A run reached without a release
  fails and says to ask the FEX work; nothing is built as a fallback.
- the main build (the `Fetch the tuned FEX` step of `build.yml`, into
  `package-autorun.py --extra`) stages it with `fex-variant.txt` set to `opt`,
  so the SD archive runs the tuned variant out of the box and returns to the
  pinned ones when that file is deleted. `verify-package.py` knows the two
  variant module names as the one thing in the package not from the DLL
  repository.

`check_fex_payload_source.py` (run by the Build job's host tests) reads both
workflows back and stops the world if either starts compiling, tuning or
publishing FEX again: a second builder is how the A/B ends up comparing builds
nobody chose, which is exactly what happened while the startup crash was
bisected.

## Handing over an optimisation

The tuning loop is: a run on the console, its log, a proposal, then the FEX
work compiles it. This branch does not compile anything, so a proposal has to
carry what the FEX work needs to build it on its own:

- **What to change** — the knob or the source-level change, by name: a CMake
  input (`TUNE_CPU`, `TUNE_ARCH`, `FEX_TUNE_TEGRA_X1`, `ENABLE_LTO`, …), a
  patch against the FEX tree, or a change in `horizon.patch`'s side of the
  Horizon layer. One change per proposal, so an A/B can attribute it.
- **Why** — the evidence in the log, quoted by line: which `[PROF]`,
  `[CPU]`, `[EXC]` or timing line shows the cost, and what the change should
  take off it. "It might be faster" is not a proposal.
- **How it lands** — the module names it should keep (`-opt`), whether the
  stock modules must stay untouched, and any ABI the runtime has to match
  (`horizon-wine/runtime-interfaces.json`; `fex_payload.py` checks it).
- **How to tell it worked** — the run to make and the lines to compare with
  the previous build's, e.g. the same game, the same scene, `[CPU]` naming the
  intended module and the `[PROF]` total for the frame loop.
- **The risk** — what breaks if the CPU or the tree is not what it is assumed
  to be (the ARMv8.1+ assumptions, the W^X aliasing, the I-cache flush), and
  which of those a failure in the log would look like.

A proposal in that shape is something the FEX work can build and publish
without a round trip; anything less comes back as a question.

## The FEX-2610 port, and the patch that makes it usable

`berlint-hub/FEX@6ec29a2e` is FEX-2610 with the Horizon port, and its
**memory layer is fallback stubs**: `Source/Windows/Common/Horizon.cpp` only
answers address-space questions, `CRT/HorizonHeap.cpp` hands the pointer back
unchanged from `HorizonWritableAddress` (there is no writable alias for the
W^X code buffers) and flushes with `__builtin___clear_cache`, which does
nothing on Windows -- so translated code runs without the I-cache ever being
synchronized, and the module dies with `STATUS_ILLEGAL_INSTRUCTION` at its
first translations (reproduced on the Switch, a module from that source).

`fex-2610-horizon-memory.patch`, in this directory, supplies the layer that
works: JIT buffers allocated through the runtime's unixlib
(`WINE_NX_FEX_ALLOC`, a readable-executable mapping with its writable alias
in `jit_registry.h`'s registry), writes through `HorizonWritableAddress` to
the alias, an I-cache flush through `WINE_NX_FEX_FLUSH`, the W^X transition
through `WINE_NX_FEX_PROTECT_CODE` (the SMC hooks `InvalidationTracker`
needs), and the CRT heap over `WINE_NX_FEX_HEAP_ALLOC`. It is the layer
`horizon.patch` carries for FEX-2609, ported onto FEX-2610's source: the
`CodeBuffer` constructor grew a `ShouldBeNamed` argument, the CRT allocator
selection moved into `HorizonHeap.cpp`, and the ARM64EC thread entry is a
different function -- each placed by reading that tree, which is why what has
been compiled and what has not is stated plainly.

The compile fixes that port used to need on its own (`tegra-x1-fixes.patch`:
declarations in `Horizon.h` instead of inline definitions, the
`SupportsAVX256` line FEX-2610 dropped, `CodeBuffer::Contains`) are already
in `6ec29a2e`, together with the Tegra X1 tuning (`FEX_TUNE_TEGRA_X1`) and
the CMake plumbing. What a builder needs beyond the tree is the patch, the
ABI headers from this directory, and the two compiles -- see
`builder-handover.md`, which is the note to hand over.


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
