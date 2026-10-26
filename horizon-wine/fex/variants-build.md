# FEX variants: the stock release and a tuned build, side by side

The DLL payload can carry two FEX CPU-module builds, so the two can be
compared on the Switch without rebuilding or reinstalling:

- **stock** — `libwow64fex.dll` and `libarm64ecfex.dll`, from the pinned
  `FEX-Emu/FEX@395b132f` (FEX-2609) with `horizon.patch` applied on top,
  exactly as the release pipeline has always built them;
- **opt** — `libwow64fex-opt.dll` and `libarm64ecfex-opt.dll`, from
  `berlint-hub/FEX@b8674872` (FEX-2610 with the Horizon patch already in its
  tree), built with the Tegra X1 (Cortex-A57) tuning and
  `tegra-x1-fixes.patch`, the port's compile fixes.

Both share Autorun's ABI headers in `horizon-wine/fex/` and are validated the
same way; only the CPU back end and the version resources differ.

## The build side

The changes live in the `autorun-horizon-dlls` tools and are kept here as
`dlls-fex-variants.patch` (from `berlint-hub/autorun-horizon-dlls`, branch
`arena/6fd3131b-fex-variants`) until they land upstream:

- `tools/build-fex.sh` takes `WINE_NX_FEX_REMOTE`, `_REVISION`, `_PATCH`
  (empty for none), `_TUNE_TEGRA_X1`, `_VERSION` and `_SUFFIX`, and gives the
  modules and their payload directory the suffix when one is set — the recipe
  names `tegra-x1-fixes.patch` as `_PATCH`, and `FEX_TUNE_TEGRA_X1=ON`, the
  tuning option berlint-hub/FEX defines, as the CPU tuning;
- `tools/fex_payload.py` takes `--suffix`, `--version`, `--revision` and
  `--patch`, so a variant's manifest honestly records its own source;
- `tools/build-dlls.py` builds the variant as the `fex-opt` entry, from the
  recipe in `FEX_VARIANT`, beside the pinned `fex` one.

To land it: apply the patch on top of the `horizon-dlls` submodule pin
(`git -C horizon-dlls am ../../horizon-wine/fex/dlls-fex-variants.patch`),
push, and update the pin here; or open it as a pull request against
`autorunhq/autorun-horizon-dlls`. `build.sh --dlls` then produces both module
pairs in `switch/wine/drive_c/windows/system32/`.

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
release appears, the download takes over without any change here. The compile
fixes the FEX-2610 port needs (`tegra-x1-fixes.patch`) belong in the FEX
branch itself; until they are merged there, both build paths apply them on
top.

The main build takes the modules the same way (the `Fetch the tuned FEX` step
of `build.yml`, into `package-autorun.py --extra`): whichever `fex-opt`
release exists is checked and staged with `fex-variant.txt` set to `opt`, so
the SD archive runs the tuned variant out of the box and returns to the
pinned ones when that file is deleted. `verify-package.py` knows the two
variant module names as the one thing in the package not from the DLL
repository.

## A/B on the Switch

`sdmc:/switch/wine/fex-variant.txt` holds the variant suffix (`opt`) or is
absent/empty for stock. The runtime loads the variant's WoW64/ARM64EC CPU
module when the file names a suffix and that module is present; if the module
is missing it falls back to the stock one. The start log's `[CPU]` line says
which one runs:

```
[CPU] FEX (libwow64fex-opt.dll)     <- opt variant
[CPU] FEX (libwow64fex.dll)         <- stock
```

Delete the file (or write `stock`) to go back; no reinstalling is needed.
