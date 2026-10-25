# Building two FEX builds for comparison

The runtime can pick between the stock FEX CPU modules and a differently
built pair (see `runtime_fex_cpu_module` in source/runtime.c): put the
variant's name in `sdmc:/switch/wine/fex-variant.txt` and the program
loads `libwow64fex-<variant>.dll` / `libarm64ecfex-<variant>.dll` when
they are in system32. An empty file, or modules that are not there, keep
the stock ones.

The modules are the DLL repository's to build
(autorunhq/autorun-horizon-dlls, `tools/build-fex.sh` and
`tools/fex_payload.py`). The stock pair stays as it is; a variant pair
is the same scripts with different inputs. The Tegra X1 build is
`berlint-hub/FEX` branch `arena/799d045b-fex`, commit
`2df7c59b234ec70b7221e054802fa62853f9cb45` (FEX-2610 with the Horizon
port already in it), so no patch is applied on top.

## tools/build-fex.sh

Take the pin, the patch and the tune from the environment, with today's
values as the defaults:

```sh
 revision=395b132f346b1a45def246d10c52245edba1ef02
+revision="${FEX_REVISION:-$revision}"
+remote="${FEX_REMOTE:-https://github.com/FEX-Emu/FEX.git}"
+name_suffix="${FEX_NAME_SUFFIX:-}"
+apply_patch="${FEX_PATCH:-1}"
+tune_tegra="${FEX_TUNE_TEGRA_X1:-OFF}"
+override_version="${FEX_OVERRIDE_VERSION:-FEX-2609}"
```

Use `$remote` in the clone (`git remote add origin "$remote"`), skip the
patch block when `apply_patch` is 0 (the tree already carries it), pass
`-DFEX_TUNE_TEGRA_X1=$tune_tegra` and
`-DOVERRIDE_VERSION=$override_version` to `build_module`, and hand
`$name_suffix` to the payload step so the DLLs are written as
`libarm64ecfex$name_suffix.dll` and `libwow64fex$name_suffix.dll`.

## tools/fex_payload.py

Take the suffix as a second argument (or an environment variable) and
build the `DLLS` dictionary names with it; the manifest keys follow the
renamed files.

## tools/build-dlls.py

A second entry beside `key='fex'`, building the same two DLLs under
another name from another pin:

```python
def build_fex_opt(scratch):
    env = dict(os.environ,
               WINE_NX_FEX_DIR=str(horizon_wine / 'toolchains/fex-2610-tegra'),
               WINE_NX_FEX_BUILD_DIR=str(horizon_wine / 'toolchains/build-fex-2610-tegra-horizon'),
               FEX_REMOTE='https://github.com/berlint-hub/FEX.git',
               FEX_REVISION='2df7c59b234ec70b7221e054802fa62853f9cb45',
               FEX_PATCH='0', FEX_TUNE_TEGRA_X1='ON',
               FEX_OVERRIDE_VERSION='FEX-2610', FEX_NAME_SUFFIX='-opt')
    ...run tools/build-fex.sh with that environment...
```

with its manifest identity kept apart from the stock pair, so both are
installed and the card can hold them at once.

## Comparing

With both pairs in `drive_c/windows/system32`, one line decides which
runs -- `sdmc:/switch/wine/fex-variant.txt`:

```
opt
```

The log says which one it was: `[CPU] FEX (libwow64fex-opt.dll)`. Take
the file away (or leave it empty) for the stock pair, and the same game
runs `[CPU] FEX (libwow64fex.dll)`.
