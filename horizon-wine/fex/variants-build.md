# FEX variants: the stock release and a tuned build, side by side

The DLL payload can carry two FEX CPU-module builds, so the two can be
compared on the Switch without rebuilding or reinstalling:

- **stock** — `libwow64fex.dll` and `libarm64ecfex.dll`, from the pinned
  `FEX-Emu/FEX@395b132f` (FEX-2609) with `horizon.patch` applied on top,
  exactly as the release pipeline has always built them;
- **opt** — `libwow64fex-opt.dll` and `libarm64ecfex-opt.dll`, from
  `berlint-hub/FEX@b8674872` (FEX-2610 with the Horizon patch already in its
  tree), built with the Tegra X1 (Cortex-A57) tuning.

Both share Autorun's ABI headers in `horizon-wine/fex/` and are validated the
same way; only the CPU back end and the version resources differ.

## The build side

The changes live in the `autorun-horizon-dlls` tools and are kept here as
`dlls-fex-variants.patch` (from `berlint-hub/autorun-horizon-dlls`, branch
`arena/6fd3131b-fex-variants`) until they land upstream:

- `tools/build-fex.sh` takes `WINE_NX_FEX_REMOTE`, `_REVISION`, `_PATCH`
  (empty for none), `_TUNE_TEGRA_X1`, `_VERSION` and `_SUFFIX`, and gives the
  modules and their payload directory the suffix when one is set;
- `tools/fex_payload.py` takes `--suffix`, `--version`, `--revision` and
  `--patch`, so a variant's manifest honestly records its own source;
- `tools/build-dlls.py` builds the variant as the `fex-opt` entry, from the
  recipe in `FEX_VARIANT`, beside the pinned `fex` one.

To land it: apply the patch on top of the `horizon-dlls` submodule pin
(`git -C horizon-dlls am ../../horizon-wine/fex/dlls-fex-variants.patch`),
push, and update the pin here; or open it as a pull request against
`autorunhq/autorun-horizon-dlls`. `build.sh --dlls` then produces both module
pairs in `switch/wine/drive_c/windows/system32/`.

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
