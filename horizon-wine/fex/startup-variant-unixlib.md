# FEX `-opt` startup failure: static unixlib lookup

## Finding

The original Horizon static unixlib table in `dlls/ntdll/unix/virtual.c`
registered only `libwow64fex.dll` and `libarm64ecfex.dll`, matched exactly
(case-insensitively). Renaming the PE files for a variant does not change
FEX's unixlib ABI, but it **does** change the loader's `BaseDllName`, which
ntdll uses to find the static unixlib.

FEX's `Source/Windows/Common/FEXUnixLib.cpp` tries two methods:

| Method | Requested identity | Original result |
| --- | --- | --- |
| `MemoryWineLoadUnixLibByName` (1002) | `libwow64fex` / `libarm64ecfex`, **without `.dll`** | No match, including for stock |
| `MemoryWineUnixFuncs` (1000), with `&__ImageBase` | Loader entry's `BaseDllName` | Stock `.dll` matches; `-opt.dll` does not |

The fallback uses the native loader list, not the PE export-directory name.
The latter is used only in the separate x86/WoW64 unixlib path. The FEX CPU
DLL is a **native** module even when it translates x86.

## Why this fits the reported status and timing

The working `horizon.patch` contains this startup chain:

```
Wow64LdrpInitialize
  process_init
    BTCpuProcessInit
      InitCRTProcess
        HorizonInitialize
          UnixLib::Init    (both lookups miss for -opt)
        NtTerminateProcess(..., STATUS_DLL_INIT_FAILED)
```

`Source/Windows/Common/CRT/CRT.cpp` explicitly terminates with
`STATUS_DLL_INIT_FAILED` (`0xc0000142`) if `HorizonInitialize()` fails.
This runs **before** constructors, logging initialization, the config-file
access and all guest execution. `DllMainCRTStartup` itself returns true, so
successful native DLL load/attach breadcrumbs do not contradict this.

This is a source-level explanation of the reported failure, not a claim of a
successful Switch retest. The unixlib QUERY/exception-attachment step can
still fail for other reasons once name resolution succeeds.

The pointer breadcrumbs for `BTCpuProcessInit`, `BTCpuThreadInit` and
`BTCpuSimulate` in game-branch diagnostic commit `09c547e` precede their
`GET_PTR` assignments. Zero there does not establish a missing export; the
before/after-call breadcrumbs are the useful ones.

## Fix and regression coverage

FEX has a separate, feature-guarded static table matched by canonical stem.
The stem, stock `.dll` name and suffixed variant names resolve to the same
function table. Other unixlibs retain their exact-name rules. The returned
static module handle is stable and recognized by unload. Malformed byte
lengths and empty/invalid suffixes do not match.

Run on an ordinary Linux host:

```sh
CC=cc python3 horizon-wine/tests/check_fex_unixlib.py
```

This compiles the **actual tables and resolver/unload/module-list functions
extracted from `virtual.c`**, using Wine's real types. Only unixlib
implementations and the current TEB are stubbed. It checks stock and variant
names for both CPU-module architectures, the by-name and native module-base
paths, case folding, bounded non-NUL-terminated names, stable/unloadable
handles, unchanged non-FEX rules, and existing initializer failures. All four
combinations of FEX on/off and the other optional modules on/off run with
ASan/UBSan. `horizon_unixlib_names.c` also tests the matcher itself.

For a regression check, `--source /path/to/old/virtual.c` runs the same cases
against the old resolver. The original passes stock module lookup but fails
on the `-opt` loader name.

## Where this stands

The fix is merged into the game branch (merge `5b21aa3`): `virtual.c` pairs a
FEX CPU module by its canonical stem, and `check_fex_unixlib.py` (run by
`horizon-wine/check-runtime-console.sh` and `check-fex.sh`) exercises the real
resolver with stock and `-opt` names under every build's feature guards.

`virtual.c` is compiled into `wine-virtual-real` and linked into the runtime
NRO, so what a card needs is a runtime rebuilt from a tree that carries the
merge -- not just a new `ntdll.dll` or new CPU DLLs. The `-opt` modules are
tuned again (`variants-build.md`), which is the state the A/B was for.

## On the Switch

Compare stock FEX and FEX optimized with the same title:

1. Confirm `[CPU]` names the intended module in each run, and that
   `switch/wine/fex-opt-manifest.json` says `"tune": "cortex-a57"` for the
   modules on the card (a control build says `null`).
2. With `-opt`, check that `[WOW64-DIAG] BTCpuProcessInit call` is followed by
   config-file activity and `BTCpuProcessInit returned`, then `cpu_simulate`.
3. Verify that startup reaches the game and no longer exits at the initial
   `0xc0000142` window. Recheck stock FEX as the control.
4. If a tuned build still fails at startup while the stock build passes, run
   the **FEX variant** workflow with `tune: off` and try the twin under the
   same names. The twin starting and the tuned build not is the tuning's
   problem; both failing is the variant path, and the fix above is not the
   whole of it.
5. Keep `stderr.txt` and the runtime log from the failing build either way.
   Missing static FEX mappings are named in the Wine error log by
   `wine_nx_report_unpaired_unix_lib()`; distinguish that from a later
   QUERY/exception-attachment failure.

The incomplete FEX-2610 fallback memory layer is a separate problem and is
not repaired by this name-lookup change.
