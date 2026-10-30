# Autorun continuation handoff

**Updated:** 2026-10-30 (carried forward on `arena/d0bc27f7-autorun`)
**Branch:** `arena/97e282a9-autorun`, continued on `arena/16b1334a-autorun`, carried
forward on `arena/d0bc27f7-autorun` (its fast-forward descendant: all 164 commits of
`arena/16b1334a-autorun` were merged in on 2026-10-30, and the five host checks and
four `cc` host tests named below pass on it)
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

## Display/swapchain review findings (2026-10-28, all answered)

1. **OpenGL and the launcher's Aspect/Integer scaling.** The WGL/OpenGL path
   hands its buffer straight to the display's scaler, which stretches it, so
   Aspect and Integer cannot reach an OpenGL picture without an intermediate
   panel-sized pass (a compositor layer, or a readback and blit) that this
   tree does not have for GL. What is fixed: the fit the input maps through now
   follows what the window really shows (`wine_nx_present_fit_stretched`,
   `wine_nx_screen_crop_4_3`), including the central 4:3 crop, which used to be
   cut into the buffer while the input still mapped the uncropped picture; and
   the first swap logs once that the display stretches an OpenGL surface when
   the setting asked for something else, so a player is not left guessing.
   `documentation/technical.md` says the same. Doing more means presenting GL
   through the compositor: a bigger change, and one that needs a console to
   judge.
2. **Screen-size failures.** `screen_take_dimensions()` now answers whether the
   window took the program's size. When it did not, the crop is not cut (a crop
   for a size the window does not have cuts the wrong part), and the log names
   the size the surface presents at against the size the window kept.
3. **Vulkan screen ownership** is counted, not one flag: a program with two
   surfaces, or a device reset whose new surface is made before the old is
   destroyed, no longer has the screen handed back under it.
4. `horizon-wine/tests/check_gl_surfaces.py` was brought back to what it
   slices: it builds with `cc` (`WINE_NX_HOST_CC`) and passes, including the
   size a swap publishes and a Vulkan stand-down.

Also fixed from the same review: `wine_nx_compositor_suspend()` waits for the
presenter with a three-second bound instead of for ever; a presenter or input
thread that will not stop is reported to `return_to_launcher()`, which then
keeps the process out of the loader's hands; `release_screen_buffers()` takes
the screen's lock with a bound; and a hidden popup no longer presents the same
frame twice.

Eight host tests that slice the driver (`check_pointer_events`,
`check_popup_restore`, `check_gl_surfaces`, `check_render_overhead`,
`check_horizon_thread_fds`, `check_capture_context`,
`check_wow64_unix_tables`, `check_horizon_thread_handles`) had drifted from
the code they slice; they build and pass again and now run in CI.

These were static review findings; they were fixed and host-tested here, not
confirmed on a Switch. The user plays handheld and does their own device
testing.

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

- **[P2] Cleanup continues after worker-stop timeouts, creating a hang/race window.
  Answered:** `wine_nx_input_thread_stop()` and `wine_nx_compositor_stop()` now
  answer whether the thread ended, and `return_to_launcher()` keeps the process
  out of the loader's hands when one did not; `release_screen_buffers()` takes
  the screen's lock with a bound and says so when a stuck thread holds it.
- **[P2] (original wording) Cleanup continues after worker-stop timeouts.** `return_to_launcher()` ignores the status of `run_closing_step(stop_mesa_workers, ...)` and proceeds after `wine_nx_input_thread_stop()` and `wine_nx_compositor_stop()` merely log a timeout (`runtime.c:4656–4673`, `compositor.c:241–257`, `winnx_drv.c:112–137`). It then releases screen buffers and GPU sessions (`runtime.c:4114–4135`, `4211–4230`). In particular, `wine_nx_fb_present()` holds `wine_nx_fb_mutex` while calling `framebufferBegin/End` (`runtime.c:1567–1601`), whereas `release_screen_buffers()` takes that same mutex without a timeout (`runtime.c:4114–4135`). If a framebuffer call stalls while holding the mutex, the nominally bounded shutdown can block forever there. If a compositor/Mesa worker remains in GPU work, driver/surface teardown may also overlap it. The final memory guard should prevent reloading while a worker's stack is still borrowed, but it runs only after these teardown operations. Consider propagating stop status and avoiding resource teardown/reload while a worker may still use those resources; verify fail-closed app exit behavior on device.
- **[P2] Shutdown thread registry reads race with registration/unregistration.
  Answered:** the slot's handle is the flag that says the slot holds a thread,
  and it is now written last on registration and read atomically, so a reader
  sees either a free slot or one whose fields are already written
  (`registry_handle`, `registry_set_handle`, `source/thread_profile.c`);
  `parked` is read and written the same way.  `threads_profile.c`'s host test
  builds and passes with `cc`.
- **[P2] (original wording) Shutdown thread registry reads race.** `wine_nx_threads_wake()`, `wine_nx_threads_program()` and `wine_nx_threads_other()` read ordinary `registry[]` fields without `registry_mutex` (`thread_profile.c:325–375`), while `wine_nx_thread_register()` and `wine_nx_thread_unregister()` mutate them under that mutex (`294–325`, `378–390`). During cooperative shutdown this is a C-level data race and may produce an inconsistent worker count/wake list. The later borrowed-memory guard reduces the risk of handing a live worker's stack to the loader, but does not prevent overlap with earlier graphics teardown. Consider a safe snapshot/atomic-handle design and a concurrency test.

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

1. Read this file and inspect `git status` before changing anything; the work
   branch is `arena/16b1334a-autorun`.
2. Preserve unrelated FEX/Zink work and keep all changes on this branch.
3. The remaining display work that needs a console: presenting OpenGL through
   the compositor, which is the only way the Aspect/Integer settings can reach
   a GL picture. Everything else from the review is answered above.
4. Add bounded retention for per-run logs/reports before release.
5. The user still needs to test this build on a handheld Switch, especially
   repeated start/normal exit/forced quit, two Vulkan surfaces alive at once,
   and memory reuse; do not claim hardware safety without that test.

## The 2026-10-29 logs: Riddick never swaps, Doom 3 stays on NVC0, Zink never appears (2026-10-29)

Read from the run logs the user attached (`doom3_*.log` ×3, `DarkAthena_*.log` ×2,
`autorun_runtime.log`, `driver.txt`, `stdout.txt`). What they actually say:

- **Doom 3 runs, on NVC0.** `[MESA] OpenGL renderer: native NVC0` in all three runs,
  `EGL Mesa Project 1.4` with 21 configs, a 4.3 Compatibility context, an extension
  list of 7743 bytes, and swaps that come back in 0-7 ms (`swap #1 came back after 7
  ms`), `gl_frames=479` over ten seconds once the level was up. Nothing in these logs
  is an error line. "Zink does not run in Doom 3" is not a rendering failure here: the
  program asked for NVC0 (`runtime_gl_driver == 0`), and no `[SETTINGS]` line exists in
  any of the three runs, i.e. `sdmc:/Roms/Windows/Doom 3/doom3.wine-nx.txt` was not
  read at all -- so the per-game setting the launcher shows was never applied. The log
  could not say that before; now it does (`[SETTINGS] … is not there`).
- **Which renderer answered is now measured, not assumed.** `[NXGL] the first context
  made current is …` was already there; the line under it now compares that with
  `MESA_SWITCH_GL_DRIVER` (`source/nxgl_renderer.h`, `tests/nxgl_renderer.c`, CI "Which
  OpenGL renderer answered") and says `it is` or `it is NOT the renderer this program
  asked for`. `[MESA] OpenGL renderer asked for: …` is worded as the asking.
- **A terminated EGL display does not lock the backend.** mesa-switch's
  `switch_terminate()` (read at HEAD, `src/egl/drivers/switch/egl_switch.c`, 1506)
  releases the display data, so the next `eglInitialize()` re-reads
  `MESA_SWITCH_GL_DRIVER`. The launcher's SDL display therefore does not stand in a
  program's way; a *live* display does -- the compositor's, which outlives the program.
  The earlier note in this file that the launcher shares one display with the program
  was wrong, and the comment in `runtime.c` and `documentation/technical.md` say so now.
- **Riddick's hang is not a swap.** Dark Athena's run has zero swaps: the last GL line
  is the context introspection, and the stall report has the main thread (`tid 4w` at
  99.8 % of a core) inside a GL call while the screen's window reports `3 slots
  registered, 800x600 format 0, connected, a buffer is out and has not come back`. Five
  compositor frames are on the glass, the program's own OpenGL has presented none, and
  the clocks keep moving -- it is a wait for a buffer nobody returns, not a stall in
  time. The compositor had just started (`[NXCOMP] presenting the screen through OpenGL
  on NV12B`) and handed the window over right before it.
- **The `[SETTINGS] … is not there` line was wrong in its first version.** It hung
  off `launcher_kv_load()`'s answer, and that answers 1 for a file that is not
  there -- an empty settings set is a valid answer -- so it never fired for the
  case it named. It now asks `access()` inside the block that reads the file, and
  `tests/launcher_settings.c` pins the loader's behaviour so the mistake is not
  made again. The three Doom 3 runs have no `[SETTINGS]` line at all, which a
  current build always writes; those runs predate the summary.
- So the tree now releases what the suspended presenter left on the window.
  `wine_nx_compositor_suspend()` returns whether the presenter really stepped aside
  (its surface destroyed), and `wine_nx_screen_acquire()` releases the window's buffers
  before taking it, only in that case and only when this call is the one taking the
  screen from the compositor (a Vulkan swapchain reset acquires the screen it already
  has). It says how many it found (`[NXGL] the screen still had N buffers registered
  for the compositor: released`). This is a plausible cause for the buffer-out wait and
  is **not** proven: no console was available, and the line above is what the next run
  has to show or not show.
- **ARM64 frames are named now.** `nx_prof_pe_export()` read only PE32 optional
  headers, so every ARM64 module -- wow64.dll's thunks, ntdll's PE half -- came out as
  `module+0x…` in `[STALL]`/`[PROF]`. Both layouts are read (`source/thread_profile.h`),
  and `tests/thread_profile.c` covers a PE32+ image, a PE32+ magic over the 32-bit
  layout, and an unknown magic.

- **A spinning thread's callers are now read from its stack.** The stall report's
  `| x86 at …` had no callers for Dark Athena's main thread
  (`< 0 < 0`): the chain came from `ebp` and the game is built without frame
  pointers. `nx_prof_x86_scan()` scans the stack's own words for return addresses
  into a module when the frame chain is empty (marked `(scanned)`), host-tested.

Still open, unchanged by this: Doom 3's visual glitches (no log line describes them --
the rendering path looks healthy and no frame was lost), the 1 ms granularity of
`GetTickCount` in `KUSER_SHARED_DATA`, the alpha of the OSK overlay, and the repaint
after `wine_nx_fb_unlock`.

### What was added on top (same day): the renderer that answered, and the driver's own words

- `[NXGL] the first context made current is …` followed by `it is the renderer
  this program asked for (Zink)` or `it is NOT … the EGL display was made with
  another backend before …` — one place, once, asked of the runtime
  (`wine_nx_gl_renderer_requested()`, with the environment as the fallback).
  `source/nxgl_renderer.h` decides it (Zink names itself first in
  `GL_RENDERER`; NVC0 names the card), and `tests/nxgl_renderer.c` covers the
  strings both drivers really give — wired into CI as "Which OpenGL renderer
  answered".
- The runtime asks `wine_nx_compositor_suspend()` whether the presenter really
  stepped aside, and only then releases what is left on the window (the earlier
  note in this file said "both ways"; it was one way, and the way in — the
  compositor's leftovers — was missing).
- An EGL display that is somehow still up when the program's renderer is chosen
  is terminated there and then, so the program's own display is made with the
  requested backend (`[MESA] an EGL display was still up (…) and was let go`).
  Normally SDL has terminated the launcher's display and nothing happens.
- `gl-debug.txt` / `gl-debug` in `config/settings.json` turns on `GL_KHR_debug`
  for every context: the driver's account of a refused shader, an unknown enum
  or a lost context lands in the run log (`[NXGL] the driver (high error): …`),
  synchronously, first 64 messages, then counted. The debug bit is dropped first
  if the driver refuses such a context, so the switch can never cost a program
  its OpenGL.
- `gl-driver` in `config/settings.json` (`"zink"` or `"nvc0"`) chooses the
  renderer for programs whose own settings file names none; a program's own
  `gl-driver` still wins (`launcher_settings_gl_driver()`, covered by
  `tests/launcher_settings.c`). Three Doom 3 runs had no `[SETTINGS]` line at
  all, so their renderer setting had never reached the runtime.

### The handover, checked: who may release what (2026-10-29, later)

`wine_nx_screen_acquire()` now has a host test that runs the real function
(`horizon-wine/tests/check_screen_handover.py`, CI "The screen handover"),
against a mock NWindow with libnx's own rule in it: `nwindowSetDimensions`
refuses while buffers are registered -- even a repeat of the size the window
already has (`(nw->width || nw->height) && nw->slots_configured`) -- and
`nwindowReleaseBuffers` is what makes the next call succeed. What it pins:

- the compositor's stale slots (one of them out) are released **before** the
  window is sized, and the log says how many there were;
- a presenter that left nothing behind costs no release at all;
- a presenter that did not step aside in time still loses its slots to
  `screen_take_dimensions`'s own retry, and the log names the refusal;
- the framebuffer is closed before the surface takes the screen;
- **a Vulkan swapchain reset no longer touches the window.** This was a real
  bug: the reset acquires the screen the surface already holds, with (0,0), so
  the code asked for the panel's size, libnx refused (the live swapchain's
  buffers are registered) and the retry released them -- the buffers the
  swapchain was about to present from. Reused screens are no longer resized
  (`[NXGL] the screen a Vulkan surface reuses is WuxH, not WxH` when they
  disagree);
- an OpenGL window while a Vulkan surface holds the screen is refused and says
  so, and nothing of the Vulkan surface's is released.

## The played-time counter: who times a run (2026-10-29)

The user said the counter of played time "is somehow confused". Read back from
the code, it was confused in three ways, and all three are now fixed and host
tested.

**It was the launcher's, and the launcher is not what runs a game.** The
launcher wrote `started-at` as it started a program and the next launcher open
credited `now - started-at`. That covers a game started from the library and
nothing else: a game started from its **HOME forwarder**, from **run-next.txt**
behind a fix's setup, or from the **command line** never meets the launcher, so
its time was never counted at all, and its "last played" stayed at whatever it
was. The runtime is the one that starts every program and is still there as it
ends, so the timing moved there (`horizon-wine/source/runtime.c`):
`library_run_begin()` writes `switch/wine/play-session.txt` (path, start, last
seen up) and updates the game's `last_played`; `library_run_end()`, next to
`finish_game_report()`, credits the run through
`launcher_catalog_credit_run()` and clears the file. The launcher no longer
writes `started-at` at all; it still settles a card that carries one from an
older build, and it credits the session file the runtime left behind.

**A run that ended badly was credited up to the next launcher open.** Quit a game
from the HOME menu, switch the console off in a game, or let the runtime hang,
and the next launcher open credited everything since the program began -- the
night included -- capped at a day. The session file now carries the last time
the runtime said it was up, written once a minute from the log flusher's thread
(`library_run_keep()`, `ticks % 300`), and both the runtime and the launcher
credit no further than that.

**An unset clock wiped when a game was last on.** `p->last_played = now_seconds()`
wrote the 0 an unset clock answers over a known good time. It is only written
when the clock says something now.

Every run says what it added, so a count that still looks wrong can be read back
from the log: `[PLAY] doom3.exe began; 3h 12m played so far` and
`[PLAY] doom3.exe ran 41m; 3h 53m played so far`. `[PLAY]` is in the run
report's selected lines, and `documentation/technical.md` says the same.

Tests: `horizon-wine/tests/launcher_catalog.c` (`check_run_session`: the session
file round trip with an escaped path, torn and foreign files rejected, credit
only to the program named, backwards clock and the one-day cap, `alive` and
`now` clamps, a file for a game that left the library cleared not credited), and
the new `horizon-wine/tests/check_play_time.py` (CI "Play time a run adds"),
which runs the runtime's own block out of `runtime.c` against the real catalog
code on a temporary card.

What is left: a run that ends within its first minute, before any heartbeat, is
credited nothing -- it would have shown "<1m" anyway. The counter is wall-clock
between the program's start and its end, so a clock moved forward during a run
is still counted at its word, up to the one-day cap.

## Swapping the controller for the keyboard and mouse in game (2026-10-29)

The user asked for an in-game switch between keyboard/mouse and gamepad, with
`+` and `-` (Start and Select) as the example. That pair was already the quit
chord (held for a second), so the two are now one state machine
(`input_chord_step`, `source/input_profile.c`, tested in
`tests/input_profile.c`):

- **held for a second** -- the program is closed, exactly as before;
- **tapped** (let go before that) -- the controller swaps between the gamepad
  and the keyboard and mouse it stands in for, for the rest of the run;
- let go *after* the hold fired is still the hold, and a clock that went
  backwards is not a hold -- the release then counts as a tap.

What the swap does: `wine_nx_input_toggle_mode()` (`runtime.c`) flips
`mode` in the one live `input_profile` the whole runtime reads, so

- the pointer poll stops or starts emulating keys and the mouse
  (`emulate = profile->mode == INPUT_KEYBOARD_MOUSE`, `wine_nx_pointer_poll`),
  and the arrow starts moving with the stick (it is visible unless the program
  hides it);
- XInput and DirectInput both go through `nx_xinput_get_state_unix`
  (`source/xinput_unix.c`, `dlls/dinput/joystick_nx.c`), which answers
  "no pad" in keyboard/mouse mode, so the pad the game sees goes away with the
  swap and comes back with the next tap.

It is live for the run and writes nothing: the launcher's **Input mode** is what
a run starts on, and a game whose menus want a mouse is one tap away without
changing how it starts. The log and the run report say `[INPUT] + and - tapped:
the controller is now the keyboard and mouse|the gamepad`.

Two details that would otherwise bite:

- An overlay (the floating keyboard, the controls overlay) has the controller
  while it is up, where `+` and `-` are keys: the chord is not read then. That
  also fixes the old behaviour of the quit chord firing while the floating
  keyboard was up.
- The controls overlay publishes a whole profile after a remap. It now keeps a
  swapped mode (`runtime_input_mode_swapped`) and takes the live mode into its
  own copy (`wine_nx_input_profile_copy`, weak for the host test), so its grid
  remaps the layout the player is actually playing on instead of putting the
  card's mode back under it.

Tests: `tests/input_profile.c` grew `runtime_chord()` (tap, hold, repeat,
release-after-hold, press-again, backwards clock). The Switch build is the only
place the wiring itself (poll and toggle) is compiled.

## The in-game OSD menu: sections, and a panel that fits the screen (2026-10-29)

The user asked for "a proper game OSD with keyboard/mouse, controller and
mapping", and said the mapping overlay that `-` + left stick click opened was
"too big and not aligned with the resolution". Both were true, and both had the
same root:

- the panel was **centred** and fixed at 800 logical pixels wide with an
  8-row list, which on 720p covered more than half the screen and hid the game
  it was being tuned against;
- `wine_nx_controls_overlay_input()` did every touch hit test against a
  **hard-coded `layout(1280, 720)`** while `frame()`/`draw()` used the screen
  the presenter passed, so on any other resolution a finger landed somewhere
  else than the thing it was on.

What it is now (`source/controls_overlay.c`, `controls_overlay.h`):

- **A panel down the right side**, one margin (2.5% of the smaller side) off
  every edge, as tall as the screen allows, 560 logical pixels wide. Everything
  -- margin, width, strips, row height, text sizes -- comes from one factor,
  `layout_scale()`, of the smaller side against 1280x720; nothing is a fixed
  number of pixels. On 1280x720 the panel is 560x684 at (702,18); on 1920x1080
  it is exactly 1.5x that (840x1026 at (1053,27)), which the host test asserts.
- **Three sections, as tabs across the panel** (the Steam Deck quick-access
  shape: side-anchored panel, icons/tabs along the top, footer of button
  hints): **Input** (pad or keyboard+mouse -- the same swap `+`/`-` make, now
  also written to the program's controls, through the new weak
  `wine_nx_input_set_mode()` in `runtime.c`), **Mapping** (the old row list,
  now scrolling to the rows that fit and paged with ZL/ZR), **Mouse** (the
  speed stops, as rows rather than a grid). It still opens on **Mapping**, the
  section it was opened for.
- **One cursor per section** (`input_selected`, `map_selected`/
  `map_top`, `mouse_selected`/`mouse_top`), so coming back to a section is
  coming back to where you were. Left/Right change section, Up/Down walk rows,
  A uses the row, X clears, Y restores, B closes; a row's grid keeps
  Left/Right for its cells. Entering a section puts its cursor where the thing
  it edits already is.
- **Taps go through the same geometry**: the console's touch panel is
  1280x720, the presenters may draw the overlay for anything (the Vulkan path
  passes the swapchain extent, the compositor and the OpenGL path pass
  1280x720), so a finger's pixels are scaled into whatever screen the overlay
  was last drawn for (`touch_x_on_screen`/`touch_y_on_screen`), and the panel,
  tabs, rows, grid cells and the footer's Back are all hit-tested against the
  drawn `layout()`.
- `wine_nx_controls_overlay_tab()` is new (for tests); the tab enum
  `WINE_NX_CONTROLS_TAB_*` lives in the header, and the .c maps its own names
  onto it so the two cannot drift.
- Labels no longer truncate at 15 characters (`labels[].text` is 80 bytes now)
  and a name too long for its row is set a size smaller instead of being
  written over the value beside it.

Tests: `tests/controls_overlay.c` was rewritten for the sections -- tab
walking both ways, per-section cursors and paging, the Input section writing
`input-mode`, the Mouse rows setting and resetting `mouse-speed`, the grid and
the capture as before, taps on a tab / on a row / off the panel / on the
footer, and one tap at 1920x1080 proving the panel's pixels are scaled (the
frame geometry at both sizes is asserted). Run it with the host stubs if SDL2
is not installed: the picture itself only compiles where SDL2_ttf is.

## More of the card cache, sized by the memory a game leaves free (2026-10-29)

Asked "can't the cache be faster / the RAM used better? Dark Souls II runs with
17 % of the memory used": the console's memory is there and mostly idle, and the
card is the slowest thing in a run, so the read cache was the thing to grow and
to make cheaper. Five changes, all in `source/sd_read_cache.h`, `source/sd_cache.c`
and the tests:

- **Sized by the heap, up to 512 MB** (`sd_cache_target_chunks`: half of what is
  free above the 512 MB floor kept for the game, clamped to 32 MB–512 MB, and
  the policy is now in the header so it can be tested). It was 32–192 MB, a
  third above the floor. A log with `heap_free_mb=1683` (the Doom 3 runs) now
  gives the cache 512 MB instead of 192.
- **24 chunks a file** (3 MB) instead of 8 (1 MB): a game streaming out of one
  big package kept a few seconds of it and read the rest again.
- **Reads up to 256 KB go through the cache** (`SD_CACHE_DIRECT`, was 64 KB):
  128 KB and 256 KB loads used to bypass it entirely and stay nothing, so a
  level entered again was read from the card in full.
- **One read buffer per thread** instead of `malloc`/`free` of up to 128 KB for
  every read (tens of thousands during a load). The copy still happens outside
  the cache lock, which is why the buffer is per thread rather than shared.
- **The memory comes back when a game needs it**: `horizon_pages_alloc_any`
  (`dlls/ntdll/unix/horizon_pool.h`) offers the cache back through the weak
  `wine_nx_sd_cache_reclaim` before it reports an allocation failure, so a
  bigger ceiling cannot starve a game whose appetite grows later. The existing
  resize (every 5 s, from `mallinfo`) also shrank it, but only after the fact.

What to look at in a log: `cache_mb=<held>/<may hold>` in `[PROGRESS]`, and a
`[CACHE] the card cache went from N to M MB: … MB of heap free, … MB held` line
whenever it moves. A run whose `cache_mb` holds at its ceiling while `sd_reads`
keeps climbing is one the ceiling is still holding back; a run at 40 % of it is
not.

Tests: `tests/sd_read_cache.c` gained `test_pool_sized_by_heap` (the policy at
nothing free, 1 GB, 1.7 GB, and with its own chunks counted) and `test_deep_file`
(a file read through more than eight chunks stays cached), and
`test_least_recently_used` was made independent of `SD_CACHE_LINES`.

## What memory a run can have, and where the ceiling comes from (2026-10-29)

Asked "can Autorun just allocate 3 GB of RAM?". It cannot, and the log says why:
a Switch reserves 0.8 GB of its 4 GB, so an application may hold 3.2 GB -- but
only a process created with the 39-bit address space can be given it. The 32-bit
forwarder (the one fixed-address Win32 games need) has 4 GB of addresses for its
code, heap and every mapping together, and Horizon reports `memory=2048 MB` to
it; `svcSetHeapSize` can never be given more than that. The ceiling is decided by
the forwarder the console started, not by anything the runtime does.

So this turn:

- `[INIT] processors=… memory=… MB used=… MB` now ends with
  `(N-bit address space: the most this run can hold)` -- the one line that says
  what the whole run has to work with, instead of leaving it to be inferred.
- `[SDCACHE]` was still printing "8 per file, 32 to 192 MB" after the cache grew:
  it is now built from the header's own constants by
  `wine_nx_sd_cache_describe()`, so it cannot drift again.
- The launcher's two forwarder rows say what each is worth: the 39-bit one is
  the only process that can be given the console's whole memory for games
  (about 3.2 GB of 4 GB), the 32-bit one is given 2 GB and never more.
- `documentation/technical.md`'s Memory bullet carries the same explanation.

What this means for a 32-bit game such as Dark Souls II: its own address space
stops at 2 GB however much the console has, so free RAM cannot make the game
hold more. It can only go to what the runtime keeps in it -- which is the card
cache (`cache_mb=<held>/<may hold>`, now up to 512 MB) and the shader caches on
the card.
