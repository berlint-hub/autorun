# Autorun continuation handoff

**Updated:** 2026-10-28  
**Branch:** `arena/97e282a9-autorun` (keep working on this branch)  
**Purpose:** preserve the current task, implementation state, test/build results, and open issues for a later AI or developer.

## User request and scope

The user asked to transfer two Autorun ideas: (1) pre-launch checks for missing DLL dependencies, recognizing Microsoft Visual C++ 2015–2022 dependencies, and (2) readable per-run reports plus a run-history view. The Carafe/NSP packager idea was explicitly declined. No questions should be asked; answer in Czech.

The user then requested a follow-up review focused on swapchain/display/image rendering. They later asked to build and record the work in the repository for continuity. CPU/GPU boost controls, Carafe packaging, unrelated game fixes, and claims of Switch verification are out of scope.

## Implemented in this working branch

### DLL dependency preflight

- `horizon-wine/source/launcher_pe.h` now reads the PE import directory and exposes bounded import-name iteration. The parser validates descriptor/name bounds and rejects malformed tables.
- `horizon-wine/source/launcher.c` checks direct imports beside the executable and in the runtime's same four lookup locations: application directory, `drive_c/windows/system32`, `drive_c`, and runtime root. It deduplicates missing names and offers **Try anyway** or cancel before the run.
- Missing VC++-named imports are recognized and identified to the user. **There is no automatic download or installation of Microsoft's redistributable.** The dialog advises copying matching DLLs or installing into the Wine prefix on PC; do not describe auto-install as implemented.
- PE import tests were added to `horizon-wine/tests/launcher_pe.c`.

### Per-run logs, reports, and history

- `horizon-wine/source/runtime.c` creates unique timestamped per-run log and `.report.txt` files under `switch/wine/logs`. The old `autorun_runtime.log` remains the current runtime log.
- Completed reports include target, start time, exit outcome, loaded modules, missing-import count, and bounded relevant settings/diagnostic lines extracted from the run log. An attempt that does not return to Autorun leaves an incomplete header/report.
- `horizon-wine/source/launcher.c` adds **Game Settings → Diagnostics → Run reports**, listing recent report summaries and paths.
- `horizon-wine/source/launcher_run_report.h` shares sanitized executable naming between launcher and runtime; `horizon-wine/tests/launcher_run_report.c` tests it.
- `horizon-wine/tools/package-autorun.py` describes the feature and its VC++ installation limitation.
- **Retention is currently unbounded**: timestamped logs/reports accumulate; consider safe bounded retention before release.

### Other existing working-tree work kept intact

These were already present during the session and must not be discarded:

- Zink/native GL selection and explanatory text in `horizon-wine/source/launcher.c`, `horizon-wine/source/runtime.c`, and default settings test.
- FEX 2610 memory/teardown patch and handover updates: `horizon-wine/fex/fex-2610-horizon-memory.patch`, `horizon-wine/fex/builder-handover.md`, `horizon-wine/tests/check_fex_2610_patch.py`.
- `dlls/win32u/opengl.c` adds a final WGL swap-stage breadcrumb on the no-drawable return path. This is diagnostic bookkeeping and does not change image output.

## Display/swapchain review findings (not fixed yet)

1. **OpenGL does not honor the launcher’s Aspect/Integer scaling settings.** The WGL/OpenGL NWindow path feeds the picture directly to the Switch display scaler, which stretches it. `nx_drawable_swap()` sets input mapping with `wine_nx_present_fit_stretched()` (`dlls/win32u/winnx_opengl.c:635–643`); `nwindowSetCrop()` is only used for `STRETCH_43` (`horizon-wine/source/runtime.c:1387–1402`). Vulkan and the software compositor use `screen_fit_rect()`, but ordinary GL does not. 4:3 or other non-panel-ratio GL games can therefore appear stretched despite the launcher help promising Aspect/Integer behavior. In `STRETCH_43`, GL input mapping also omits the crop offset. A robust fix likely needs a panel-sized output surface and an intermediate GL framebuffer/blit; do not treat a simple input-transform edit as fixing the picture.
2. **Screen-size failures are logged but ignored.** `screen_take_dimensions()` returns `void`; after both `nwindowSetDimensions()` attempts fail, acquisition continues to crop/create a surface (`runtime.c:1281–1292`, `1387`). This can produce an incorrectly sized surface/image. Consider returning failure and unwinding screen/compositor ownership safely.
3. **Vulkan screen ownership is boolean rather than reference-counted.** `wine_nx_screen_acquire()` reuses the NWindow when `wine_nx_vulkan_window` is already true (`runtime.c:1337–1346`), while destroying any Vulkan surface calls `wine_nx_vulkan_release_window()` (`dlls/win32u/vulkan.c:2175–2192`, `runtime.c:1480–1491`). If an application keeps multiple Vulkan surfaces alive, destroying one can return the screen prematurely. Either reference-count surfaces or reject additional independent surfaces.
4. `horizon-wine/tests/check_gl_surfaces.py` is stale against the current `winnx_opengl.c` helper signatures/dependencies. It could not be run here because `clang` is absent; substituting GCC exposed generated-fixture errors (missing declarations/stubs and an outdated `wine_nx_gl_acquire_window()` signature). Update the test harness before relying on it as swapchain validation.

These are static review findings, not confirmed Switch failures. The user plays handheld and performs device testing themselves; no Switch screenshot/build was produced here.

## Build and verification

- Attempted `sh horizon-wine/build-amd64-components.sh`; it stopped immediately with:
  `Missing arm64ec-w64-mingw32-clang; set WINE_NX_LLVM_MINGW.`
- The environment also has no Docker command, so the documented Switch build image/devkit build is unavailable. No Switch NRO was built.
- Passing local tests/checks:
  - GCC + ASan/UBSan `horizon-wine/tests/launcher_pe.c`: all PE/icon/import tests passed (including malformed import table and damaged-image cases).
  - GCC + ASan/UBSan `horizon-wine/tests/launcher_run_report.c`: passed.
  - GCC + ASan/UBSan `horizon-wine/tests/screen_fit.c`: all cases passed.
  - GCC + ASan/UBSan `horizon-wine/tests/launcher_settings.c`: all tests passed.
  - `python3 horizon-wine/tests/check_fex_2610_patch.py`: passed.
  - `python3 -m py_compile horizon-wine/tools/package-autorun.py`: passed.
  - `git diff --check`: passed.
- The full SDL launcher host suite and Switch visual tests were not run locally; `clang`, SDL headers, Docker, and the Switch toolchain are absent.
- GitHub Actions run [40679657102](https://github.com/berlint-hub/autorun/actions/runs/40679657102) first ran the **Host tests** job successfully, but the runtime build failed compiling `launcher.c`: `access()` and `F_OK` were undeclared because `<unistd.h>` was missing.
- Added `<unistd.h>` in follow-up commit `c9ab039` and pushed it to this branch. GitHub Actions run [40680661822](https://github.com/berlint-hub/autorun/actions/runs/40680661822) then **passed both jobs**: Host tests (56s) and Build the runtime (7m22s). Build included forwarder NCA checks and uploaded the release/card archive. Artifact `autorun`, ID `12812558132`, is 19,049,272 bytes; download it from the Actions run page. Separate `test-shots` artifact contains the overlay test images.
- This is a successful CI build, not a Switch hardware/display verification.

## Next actions

1. Read this file and inspect `git status` before changing anything; continue on `arena/97e282a9-autorun`.
2. Preserve unrelated FEX/Zink work and keep all changes on this branch.
3. Prioritize the GL scaling mismatch and Vulkan surface lifetime review. Be cautious around NWindow ownership: OpenGL already uses `nx_screen_refs`; Vulkan currently does not.
4. Add a bounded retention policy for per-run logs/reports, with tests, before treating the history feature as release-ready.
5. The CI runtime build and host tests passed. The user still needs to install/test the resulting build on handheld Switch; do not claim visual fixes without that test.
