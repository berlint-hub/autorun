# Autorun continuation handoff

**Updated:** 2026-10-28 (continued on `arena/16b1334a-autorun`, see "Riddick" below)
**Branch:** `arena/97e282a9-autorun`, continued on `arena/16b1334a-autorun`
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

## Additional launch/shutdown/memory review (2026-10-28)

Static review traced the game-start path through address-space checks, optional FEX attachment, process-parameter creation, image/section mapping and Wine entry; the normal self-termination path goes through `NtTerminateProcess` → `wine_nx_leave_process` → cooperative thread park/join → graphics/service cleanup → release of Box64/FEX/code mappings → clear uncached heap attributes → a final borrowed-region check before the loader is allowed to reload. If Autorun still owns borrowed regions, it normally refuses loader handoff and closes the app instead. This is a useful fail-closed guard, not proof of hardware safety.

Open concern from the review:

- **[P2] Cleanup continues after worker-stop timeouts, creating a hang/race window.** `return_to_launcher()` ignores the status of `run_closing_step(stop_mesa_workers, ...)` and proceeds after `wine_nx_input_thread_stop()` and `wine_nx_compositor_stop()` merely log a timeout (`runtime.c:4656–4673`, `compositor.c:241–257`, `winnx_drv.c:112–137`). It then releases screen buffers and GPU sessions (`runtime.c:4114–4135`, `4211–4230`). In particular, `wine_nx_fb_present()` holds `wine_nx_fb_mutex` while calling `framebufferBegin/End` (`runtime.c:1567–1601`), whereas `release_screen_buffers()` takes that same mutex without a timeout (`runtime.c:4114–4135`). If a framebuffer call stalls while holding the mutex, the nominally bounded shutdown can block forever there. If a compositor/Mesa worker remains in GPU work, driver/surface teardown may also overlap it. The final memory guard should prevent reloading while a worker's stack is still borrowed, but it runs only after these teardown operations. Consider propagating stop status and avoiding resource teardown/reload while a worker may still use those resources; verify fail-closed app exit behavior on device.
- **[P2] Shutdown thread registry reads race with registration/unregistration.** `wine_nx_threads_wake()`, `wine_nx_threads_program()` and `wine_nx_threads_other()` read ordinary `registry[]` fields without `registry_mutex` (`thread_profile.c:325–375`), while `wine_nx_thread_register()` and `wine_nx_thread_unregister()` mutate them under that mutex (`294–325`, `378–390`). During cooperative shutdown this is a C-level data race and may produce an inconsistent worker count/wake list. The later borrowed-memory guard reduces the risk of handing a live worker's stack to the loader, but does not prevent overlap with earlier graphics teardown. Consider a safe snapshot/atomic-handle design and a concurrency test.

Other shutdown behavior confirmed by inspection: the first quit chord asks the game to terminate itself; the runtime forces teardown after five seconds or on a second press (`runtime.c:4396–4410`, 320–356). Forced termination can lose game data that was not saved; that is distinct from memory reclamation. `loader-anyway.txt` deliberately bypasses the normal outstanding-memory guard (`runtime.c:4790–4795`, 5123); keep it an explicit diagnostic override, not a default.

Verification performed for this review:

- `check_horizon_mappings.py`, `check_horizon_guest_reserve.py`, and `check_horizon_address_space_limit.py` passed.
- The host `horizon_threads.c` test passed under GCC ASan/UBSan and GCC ThreadSanitizer. The encompassing script then stopped because `check_select_wait.py` requires missing `clang`.
- `check_horizon_thread_fds.py` and `check_horizon_thread_handles.py` could not compile/run their generated/static fixtures against the current source (stale symbol/layout assumptions); TLS/suspend/fast-sync/FEX memory scripts also need missing `clang`. These are test-harness/environment limitations, not runtime failures.
- No code was changed by this review, and no Switch execution or memory-reuse/repeated-launch test was performed.

## Riddick (The Chronicles of Riddick: Assault on Dark Athena): stuck first OpenGL swap (2026-10-28, continued)

**State: not fixed.** The user's most informative run ends with the program's first
OpenGL `eglSwapBuffers` never returning. Everything below is static reading of this
tree; no log from that run, no hardware and no Switch toolchain were available here,
so no cause is claimed.

What the tree already does about it (all before this session): the first three WGL
swaps log the last stage that returned (`NX_GL_SWAP_STAGE_*`, `winnx_opengl.c`), the
drawable's swap names its surface size and how long it took, `wine_nx_gl_swap_stuck_ms()`
exposes the time inside the driver, and `[STALL]` prints the screen's window as libnx
keeps it (slots, size, format, connected, `cur_slot`).

What was added in this session (`arena/16b1334a-autorun`):

- `horizon-wine/source/nxgl_verdict.h` turns those numbers into one of six verdicts:
  no swap open, the surface never held the screen, the window is not connected,
  nothing registered, a buffer out and never returned, the buffers are another size
  than the surface, or the driver itself did not return. Each verdict writes one
  `[STALL]` line with the size, slot count and seconds behind it.
- `dlls/win32u/winnx_opengl.c` publishes the swapping surface's size and whether it
  holds the screen (`wine_nx_gl_swap_surface()`), with the client rect as the fallback
  when the driver does not answer `eglQuerySurface`.
- `runtime.c`'s stall watch builds that state and prints the verdict under the raw
  window line; `tests/nxgl_verdict.c` is the host test, wired into the CI "Host tests"
  job as "Swap stall verdict" (passes locally under GCC).

Reading the next run (the log is `logs/NAME.log`; `[NXGL]` and `[STALL]` lines):

1. Last `[NXGL] WGL swap #1:` stage says where inside win32u it stopped, before EGL.
2. `[NXGL] swap #1 began on the program's NxM surface` then no "came back" means it is
   inside the driver's present; N x M is what the driver says the surface is.
3. The `[STALL]` verdict then names which of the six it is. If it says the driver did
   not return while the window looks right (slots > 0, none out, sizes equal), the
   remaining question is inside Mesa's Switch WSI (dequeue, fence wait or queue), which
   lives in mesa-switch, not here; the next step there is to instrument that repo's
   `nwindowDequeueBuffer`/`nwindowQueueBuffer` boundaries with the same bounded lines.
4. If it says the buffers are another size, or nothing is registered, the handover in
   `wine_nx_screen_acquire()` (`runtime.c`) is the place: `screen_take_dimensions()`
   logs that it released the compositor's registrations, and its failure is still only
   logged, not unwound (finding 2 in the display review below).

Not done, still open: the GL scaling mismatch (finding 1), Vulkan screen ownership
(finding 3), the shutdown-timeout findings, and stale harnesses
`check_gl_surfaces.py`, `check_pointer_events.py`, `check_popup_restore.py`,
`check_render_overhead.py`, `check_capture_context.py`, `check_horizon_thread_fds.py`,
`check_horizon_thread_handles.py`, `check_wow64_unix_tables.py` (the 14 vendored meson/
cmake harnesses aside, they no longer match the sources they slice; none of them runs in
CI, which is why it went unnoticed).

## Sarek 1.14 (berlint-hub/dxvk-sarek at `arena/847675e2-dxvk-sarek`): the Tegra defaults (2026-10-28)

The branch is DXVK `v1.14.1` with "Tegra X1: reduce shader stutter with async
compilation + 3 compiler threads". What it actually does, read from the tree:

- `applyTegraX1Defaults()` (`src/dxvk/dxvk_options.cpp`) sets
  `numShaderCompilerThreads = 3` when 0, `framePace = "low-latency"` when empty,
  `tilerMode = False` and `maxMemoryBudget = 2048 MiB` when 0.
- `d3d9`: `numBackBuffers = 2`, `samplerAnisotropy = 4`, `maxFrameLatency = 2`
  when left at 0/-1; `d3d11`: `disableMsaa = true`, the same anisotropy and
  latency, back buffers from `dxgi.numBackBuffers`.
- `dxbc`: `fp16Shader = true` (not a config key).

Three findings, in order of importance:

1. **The tuning never reaches the console.** `DxvkAdapter::isTegraX1()` wants
   `vendorID == NVIDIA && deviceID == 0x1380`. NVK on Horizon reports the Tegra
   X1 as **0x0fe0** (`mesa-switch`, `src/nouveau/horizon/nouveau_horizon_device.c:32`)
   and passes it straight into `VkPhysicalDeviceProperties.deviceID`
   (`src/nouveau/vulkan/nvk_physical_device.c:890`), so every Tegra default in
   that fork is dead code on the Switch -- including the "3 compiler threads" the
   commit is named after. Suggested patch there: accept `0x0fe0` (or chipset
   `0x12b`, or `device_name` containing "Tegra") in `isTegraX1()`.
2. **"Async compilation" is not set.** The comment in the `framePace` block says
   `DXVK_SHADER_COMPILATION_METHOD=async`, but no code sets
   `shaderCompilationMethod`; it stays at the `dyasync` default. Setting `async`
   has a visual cost (briefly missing geometry), so Autorun does not force it.
3. `d3d11.maxFeatureLevel` is not read by 1.14 at all (its `d3d11_options.cpp`
   has no such key), so Autorun stops offering it from 1.14.

Changed on `arena/16b1334a-autorun`: `horizon-wine/source/dxvk_options.c` carries
the profile as a table (`tegra_x1_defaults`) and **writes it into the game's
`dxvk.conf`** when the setting is left at Default for Sarek >= 1.14, so the
numbers apply whether or not the build's detection fires; the options screen
shows the default as `Default (3 / Tegra)`, `Default (True / Tegra)` and so on,
and a value the player picks always wins. `tests/dxvk_options.c` covers the
values, the written lines, the player's override and the versions that must get
neither; CI runs it as "DXVK options and Sarek releases". A hand-made `dxvk.conf`
next to the game is still read after the launcher's (`runtime.c`), so it wins.

Watch on device, because these now actually happen: `d3d11.disableMsaa = True`
(image quality) and `dxvk.maxMemoryBudget = 2048` (memory pressure on a shared
2 GiB budget). Both were silently unapplied before. `dxvk.tilerMode`,
`dxvk.lowLatencyOffset`, `dxvk.shrinkNvidiaHvvHeap`, `dxvk.zeroMappedMemory` and
`dxvk.maxFrameRate` are also in that build and still have no Autorun option.

## Next actions

1. Read this file and inspect `git status` before changing anything; continue on `arena/97e282a9-autorun`.
2. Preserve unrelated FEX/Zink work and keep all changes on this branch.
3. Prioritize the GL scaling mismatch, Vulkan surface lifetime, and the new shutdown-timeout/thread-registry findings above. Be cautious around NWindow ownership: OpenGL already uses `nx_screen_refs`; Vulkan currently does not.
4. Add bounded retention for per-run logs/reports before release.
5. The CI runtime build and host tests passed. The user still needs to test the resulting build on handheld Switch, especially repeated start/normal exit/forced quit and memory reuse; do not claim hardware safety without that test.
