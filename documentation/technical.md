# Autorun: how it works

Autorun (previously Wine-NX) is Wine for the Nintendo Switch. A homebrew NRO
runs Windows programs on Horizon through libnx: 32-bit x86 programs through
Wine's WoW64, x86-64 programs through Wine's ARM64EC loader, both with Box64
as the CPU backend, and ARM64 programs natively. The Wine server, memory
management, exceptions and the display and input drivers are reimplemented for
Horizon and run inside the same process.

The project was renamed; files and code identifiers still say `wine-nx`
(`wine-nx-runtime.nro`, `WINE_NX_RUNTIME_BUILD`). The runtime's log is
`logs/autorun_runtime.log`.

For players, see the [README](../README.md).

## Status

Verified on hardware:

| Program | State |
|---|---|
| 7-Zip `7zr.exe` | Test, add, extract, rename and the benchmark all complete. |
| Wine Notepad (x86) | Menus, dialogs, fonts and the controller cursor work. |
| OpenTTD 15.3 (x86) | Runs at up to 60 fps with OpenGL, with sound effects. |
| Quake III Arena (Quake3e, x86) | The demo runs at about 37 fps at 1280x720 with OpenGL. |
| WarCraft III (x86) | In game at 24-34 fps with its OpenGL renderer, menus at about 50 fps; the intro movie plays through DirectShow. |
| Need for Speed Underground 2 | In game with sound and the controller (NFSU-XtendedInput). With wined3d, races run at roughly 20-45 fps at default detail with the CPU overclocked. With DXVK, races load and run. |
| Halo: Combat Evolved | Plays at about 30 fps under a 32-bit (no alias) forwarder. |
| Left 4 Dead 2 | Reaches the main menu. |
| Direct3D 9 test | Draws and reads every frame back: about 55 fps through wined3d, 58 fps through DXVK. |
| Vulkan test | Instance, device, memory mapped into a 32-bit program, a surface and a swapchain; 180 frames at about 61 fps. |
| Win64 validation | AMD64 CPU, threads, audio, Vulkan and DXVK Direct3D 11 tests pass. |

In progress: The Sims 2 Legacy Collection starts, talks to its launcher
emulation over LSX and loads towards its title screen, then closes. The
Ultimate Collection has not been tested.

The OpenTTD, Quake III and WarCraft III figures were measured with the Mesa 20.1
runtime; the full package now ships the Mesa 26 one.

## What is in place

- **x86 and x86-64 execution.** `dlls/winebox64` is the WoW64 CPU DLL, while
  `dlls/winebox64ec` connects AMD64 Wine code to ARM64EC. Both use Box64's
  ARM64 dynarec with separate writable and executable code mappings, as Horizon
  requires, including Box64's call/return optimization. System calls and unix
  calls from x86 code are dispatched without leaving the emulator loop; their
  result goes in Eax as wow64cpu puts it. A fault in x86 code (access
  violation, division by zero, INT3, invalid opcode) is raised into the
  program's own exception handlers with the faulting instruction's registers.
- **Wine on Horizon.** An in-process Wine server
  (`dlls/ntdll/unix/horizon*.c|h`) covers files (with Windows sharing modes),
  directories, sync objects, threads and user APCs, the registry
  (`registry/system.reg` and `registry/user.reg` on the card, over the classes
  the DLL repository ships in `horizon-dlls/classes.reg`; hives an earlier build left in
  `switch/wine` itself are moved there on the first start), message
  queues, timers, the clipboard, raw input, object directories and sections,
  including sections with no file whose views share their pages.
- **Sockets.** Overlapped Winsock on I/O completion ports: pending
  `WSARecv`/`WSASend`, `AcceptEx`, `ConnectEx`, cancellation, and the
  completion rules of wineserver's `async_set_result`. Horizon has only IPv4:
  an AF_INET6 socket is IPv4 underneath, reaching `::`, `::ffff:a.b.c.d` and
  `::1`.
- **The mouse cursor.** Horizon has no hardware cursor, so the arrow is drawn
  by us, the classic 12x19 Windows sprite in `horizon-wine/source/
  pointer_cursor.h`. Where it is drawn depends on who has the screen: the
  compositor draws it over the window layers, the framebuffer path paints it
  into the back buffer just before the frame is queued, and while a program's
  own OpenGL or Vulkan surface holds the display -- where there is no
  compositor to draw it -- it is copied into that picture just before it is
  shown, the same place the on-screen keyboard goes (`winnx_opengl.c`,
  `vulkan.c`). That copy overwrites whole pixels without blending, so it goes
  in a run of opaque sprite pixels at a time
  (`horizon-wine/source/pointer_overlay.h`, `horizon-wine/tests/
  pointer_overlay.c`); the runs are the panel's own arrow scaled onto the
  picture, so it lands where the compositor would have put it; where the
  display shows only part of a surface -- the widescreen hack crops an OpenGL
  window's buffer to its central 4:3 -- the arrow goes on that part, at that
  part's scale (`wine_nx_surface_picture`). Both copies are
  host-tested without a console: `tests/check_cursor_overlay.py` and
  `tests/check_cursor_vulkan.py` take the code out of `winnx_opengl.c` and
  `vulkan.c` as it is and run it against mocks, and
  `tests/check_cursor_mapping.py` does the same for the runtime's own placement,
  against the real `screen_fit.h`. The arrow
  follows the pointer in the desktop's coordinates and is mapped through the
  present's fit every time it is drawn, so a display mode or a surface that
  changes the screen moves it with the picture, and the stick's cursor keeps
  the same place in the new screen and can reach its edges.
- **Graphics.** Windows are drawn as layers of one OpenGL compositor, or
  straight to the framebuffer. The two are not two speeds of the same thing:
  the framebuffer receives *window pixels*, so it shows what is painted with
  GDI or DirectDraw, while a program that draws with OpenGL or Vulkan hands
  its picture to the screen through its own surface either way -- the
  compositor suspends while that surface holds the display, and the
  framebuffer never sees those frames. Choosing the framebuffer therefore
  costs nothing and gains nothing for a 3D program; it only means no other
  window, dialog, pointer or on-screen keyboard can be drawn over the game.
  The log says which of the two is showing (`[INIT] windows shown by …`) and,
  the first time a surface takes the screen with the framebuffer chosen, that
  the setting has nothing to show for this program.
  The runtime links Mesa 26 from
  [mesa-switch](https://github.com/danfromtico/mesa-switch): OpenGL through
  NVC0 by default or Zink over NVK, and Vulkan through NVK, with Wine's
  winevulkan on top. The per-game **OpenGL renderer** setting selects between
  those two paths before EGL starts. Direct3D runs through wined3d on OpenGL,
  or through architecture-specific DXVK payloads on
  Vulkan for programs set to use it. DXVK translates Direct3D and nothing else:
  a DXVK payload has no OpenGL in it, so a program set to `d3d=dxvk` that asks
  for a GL context -- its own window, a dgVoodoo or a wined3d path -- is served
  by the Mesa below, on whichever of the two renderers the setting names.
  [nxvk](https://github.com/PalindromicBreadLoaf/nxvk), another port of the
  same Mesa 26, can be linked instead of mesa-switch with
  `WINE_NX_NXVK_DIR` (see **Building**); its OpenGL half is Zink over NVK with
  no NVC0 at all. NVK does not claim Khronos conformance and said so on every
  run, which reads as a broken driver in a log; the runtime silences that one
  warning (`MESA_VK_IGNORE_CONFORMANCE_WARNING`) and the probe's own line names
  the instance and device instead.
  When a program ends up without a context, the run log says which of the ways
  it happened: `[NXGL] EGL … (OpenGL OpenGL_ES): N config(s) for OpenGL` names
  the driver and what it offers the moment a context or a surface is first
  asked for, `the first context made current is …, OpenGL …` names the renderer
  that was really made (the renderer setting says what was asked for, not what
  came back), `no context for format N …: EGL EGL_BAD_…` carries EGL's own
  error by name, and win32u counts how many of the pixel formats could carry a
  context at all. A display with no config renderable with desktop OpenGL says
  so before any of that: it is a driver built for GLES only.

  One EGL display has one backend for its lifetime, so the **OpenGL renderer**
  setting is the display's, not only the program's: whatever made a display
  first in the process fixed its backend until that display is terminated
  (mesa-switch's `switch_terminate` frees the backend state, so the launcher's
  own SDL display, made and terminated before a program is chosen, does not
  stand in the way; the compositor's display, which lives as long as the
  program, does). A second program started in the same run keeps the renderer
  the first one asked for, and the log says so when it happens (`[MESA] this
  process's EGL display is the compositor's, made with the first program's
  renderer …`). Which of the two a program really ran on is answered
  once, by the first context that is made current: `[NXGL] the first context
  made current is …` names the renderer, and `it is the renderer this program
  asked for (Zink)` or `it is NOT the renderer this program asked for (Zink):
  the EGL display was made with another backend before …` follows it. The
  comparison is `source/nxgl_renderer.h`, tested by `tests/nxgl_renderer.c`:
  Zink names itself first in `GL_RENDERER`, NVC0 names the card. A display that
  is still up (the launcher's, if SDL left it) is let go there and then, so the
  next `eglInitialize` picks the renderer up (`[MESA] an EGL display was still
  up (…) and was let go`); the compositor's is never touched — its presenter is
  presenting through it — so with the compositor running, the renderer it was
  made with is the process's. A display made after that point is what locks the
  choice for the rest of the run.
  `gl-debug.txt` (or `gl-debug` in `config/settings.json`) makes every context
  carry `GL_KHR_debug`: the driver's own account of a shader it refused, an enum
  it does not know or a context it lost is written into the run log
  (`[NXGL] the driver (high error): …`, the first 64 messages, then the rest
  counted), synchronously from the thread that caused them. The debug bit is the
  first thing dropped if the driver will not make such a context, so the switch
  that explains a broken picture can never be the reason there is none.
  A program whose per-game settings file is not there says so — `[SETTINGS]
  … is not there: this program runs on the runtime's defaults, and a setting the
  launcher shows for it has not been saved` — next to the summary line every run
  writes, because a setting the launcher shows but never saved is otherwise
  indistinguishable from one it applied. The line is decided by `access()` and
  not by `launcher_kv_load()`, which answers 1 for a file that is not there:
  an empty settings set is a valid answer, and `tests/launcher_settings.c` pins
  that. (The three Doom 3 runs of 2026-10-29 have neither line, so the build
  they came from does not write the summary at all.)
  For a program with no renderer
  of its own, `gl-driver` in `config/settings.json` chooses one for every
  program (`[INIT] the OpenGL renderer for a program without settings of its
  own: …`; an unknown name is named and leaves the build's default), and a
  program's own file still wins. The swap interval is capped at what the config offers (0
  and 1 here) rather than passed on to be refused: the frame limit is what caps
  frames, and `eglSwapInterval` has no return value anyone would read. Frames of another size than the screen's
  (800x600 or 640x480 in full screen) are scaled to it by the runtime's own
  compute blit, and the **Scaling** setting decides where they land: stretched
  over the screen, kept in proportion with bars, by whole pixel steps, or with
  the central 4:3 stretched (`source/screen_fit.h`). The same rectangle places
  the compositor's windows and maps input, so what is shown and what is touched
  agree. The setting reaches the Vulkan presents and the compositor; a program
  that draws through OpenGL puts its buffer straight on the NWindow, and the
  display's own scaler stretches that over the panel whatever the setting
  says, so Aspect and Integer do not apply to it. Input then maps through that
  stretch (`wine_nx_present_fit_stretched`) instead of the setting, and the
  first swap says so in the log when the setting asked for something else.
  Only that rectangle is dispatched: the bars are cleared once, when a
  swapchain image is first shown, and a frame after that writes the picture
  alone, with `vkCmdDispatchBase` starting the workgroup grid where the picture
  starts (`record_compute_cmd`). Without mesa-switch the
  build falls back to devkitPro's Mesa 20.1 (OpenGL through EGL and nouveau over
  nvservices). The patched Mesa 20.1 checkout the earlier runtime used is no
  longer in this tree, though the two settings it had stay readable
  (`gl-pinned-buffers-cached`, `gl-clean-before-submit`).
- **OpenGL, tuned per program.** Two of Mesa's switches are the program's own:
  `mesa_glthread=true` (a DRI option, so lower case, and set before the first
  context) marshals GL commands on a worker thread, which is what the Android
  wrappers ship on by default and what takes the driver's bookkeeping off the
  game's thread. The other is the **Older OpenGL games** row, which picks a year
  for `MESA_EXTENSION_MAX_YEAR` (2003, 2006 or 2009, off by default): Mesa then
  reports only the extensions defined by that year, and the newer ones leave
  the string. That string is the problem Mesa documents the variable for — a
  game of that era copies `GL_EXTENSIONS` into a fixed-size buffer without
  truncating it, and a modern driver's list overruns it, which is a crash or a
  black screen that a faster or older-looking driver "fixes". On Windows the
  same workaround is an NVIDIA profile's *Extension Limit*. A modern title must
  stay off it: the extensions it needs are exactly the ones being hidden.
- **A context the driver may have to be asked for twice.** Wine's own `wglCreateContext`
  asks for a compatibility profile, and a driver built around the core profile
  can refuse that outright, leaving a game of the 2000s with no context and a
  black screen. `nx_context_create` therefore retries without the profile and,
  if need be, without the version, so the program gets the driver's own default
  context instead of nothing, and says so
  (`[NXGL] the driver refused a context with…`). What it asked for and what it
  got are both in the log — `[NXGL] the program asks for…` at creation, and
  `[NXGL] the program's OpenGL: <version> (GLSL <version>, a
  core|compatibility profile), its extension list is N bytes` when that
  context first holds the program's window. Those are the pair of facts that
  decide whether a game's `#version 120` shaders can compile at all and whether
  its extension buffer is overrun, and taking them when the context is made
  current rather than at the first frame means a game that never draws still
  says what it was given; a context the driver refuses is logged too (`the
  program's OpenGL context could not be made current, error …`), because that
  answer exists nowhere else.
- **A clock that cannot stand still.** `NtQuerySystemTime` is what a program
  reads when it waits for a moment in time — `Sleep`, a frame pacer, a timeout
  around a load — and on the console the calendar clock behind it belongs to
  the system's time services, which this runtime's C library may not have at
  all: `gettimeofday` then fails or answers the same value every time, and a
  program waiting for a deadline that never arrives looks exactly like one
  waiting for an event nobody sets. (Dark Athena was this: its main thread on a
  full core of `NtQuerySystemTime` calls, not one frame in three minutes. Its
  clock moves now and the frame still does not come, so the cause is further
  in — which is what the two reports below are for.)
  Windows itself keeps the system time as the calendar clock read once at boot
  plus interrupt time, never re-reading the RTC, so that is what the Switch
  build does now: one read of the calendar clock, cached, plus time since boot.
  It never steps backwards, it never stops, and it answers in a tick read
  instead of a round trip to the time service — the shared-data clock thread
  alone asks for it a thousand times a second. If the clock is unreadable the
  program starts from 2000-01-01 and still moves; the log says which
  (`[CLOCK] the console's calendar clock says …` or `this program has no
  calendar clock to read`), and every stall report carries what both clocks
  did since the previous one (`[STALL] the clocks moved: …`).
- **A thread the runtime is holding.** To keep the kernel's thread-local pages
  out of the program's memory, the runtime creates placeholder threads before
  the program starts and leaves one behind per page worth pinning. Each of
  those is one of the 96 slots an application may have, so a program that wants
  many threads has fewer than it should -- and a page above 4 GB, which a
  32-bit program's memory can never touch, is pinned for nothing. Only pages
  under 4 GB are held now, and when the kernel refuses the program a thread
  anyway, the placeholders are released and the create is tried once more
  (`[THREAD] the kernel refused a thread; the runtime's N placeholder threads
  were released and the next attempt made it`). The promise that no new
  thread-local page is placed later is what is given up, and only after the
  program has run out of slots, where a failed create is usually fatal --
  a `std::thread` constructor throws, a loader stops.
- **A memory report a program can size its caches by.** Every program that
  streams -- and a game of the 2000s streams everything -- asks Windows how
  much memory it may use before deciding how large its level cache is. The
  answer was built from Horizon's figure for the process, and libnx claims the
  whole memory grant as its heap at startup: a program that had done nothing
  read "1907 MB of 2048 used" and was offered 140 MB, and Dark Athena read
  19 MB of its level, stopped, and parked every thread with nothing in the log
  to say why. Free is now what the allocator can still hand out, from its own
  books (the same figure the runtime prints as `heap_free_mb`), so the same
  moment reads 900 MB free and the log's `[INIT] heap pool 0x…..0x… = N MB`
  line says how large the pool behind it is. (The opposite failure lived here
  once: AvailPageFile at 4 MB, and Fallout New Vegas, which wants 512 MB, quit
  with "Not enough memory to run application.")
- **A stall that names the call it is standing in.** `[STALL]` reports where
  every thread is, and a thread inside ntdll says only "ntdll". The system call
  dispatcher therefore keeps, per thread, the number of the call it was last
  in (`wine_nx_syscall_last`), and the report names it from Wine's own call
  tables -- `thread 4w at ntdll.dll+0x6d024 … NtUserMsgWaitForMultipleObjectsEx
  (0x11d4) x1234`, name, number and how often it was asked for since the last
  report. Waiting, spinning and locked out all look the same from outside and
  each needs a different fix; the call and its rate are what tell them apart.
  It comes with a line about the program's OpenGL (`has not swapped a frame
  yet; its OpenGL is 4.3 (Core Profile) …`), which separates "no usable
  context" from "never reached the first frame".
- **A call stuck, and a call retried forever, look the same.** A program
  sitting in one of the OpenGL entry points -- `wglSetPixelFormat`,
  `wglMakeCurrent`, `wglCreateContext`, `wglSwapBuffers` -- shows up from
  outside as no frames and one core, exactly like a program retrying a call
  that fails. Each of them is therefore counted per thread, the first call and
  each doubling are logged with the arguments and the previous result, and the
  call a thread is inside is reported by name in its stall line (`| inside
  wglSetPixelFormat`). A call made a million times costs about twenty lines.
  The slot a thread's call is recorded in holds the thread id one above itself,
  so that a slot nobody has filled -- which reads as "thread 0, call 0" -- does
  not claim that thread 0 is inside `wglSetPixelFormat`; thread 0 is a real
  entry in the same report, and it was named after a call it had not made.
- **A window that cannot have the screen draws offscreen.** A second window
  surface of a different pixel format cannot be made on the same NWindow, and
  failing the call leaves a program with no context at all -- Dark Athena
  answers a failed `wglSetPixelFormat` by asking again, which is a loop with one
  core and no frames. Such a window now gets a pbuffer drawable instead (and so
  does one whose window is hidden, and one that loses the screen to a Vulkan
  surface), a swap on it succeeds without presenting anything, and the log says
  once per doubling how often that happened.
- **A window keeps the buffers it was given.** libnx refuses to resize a
  window that still has buffers registered, and destroying an EGL surface does
  not release them. The compositor handed the screen to Dark Athena's OpenGL,
  the program asked for 800x600 and was refused without a word, its first
  frame went into a window still sized to the compositor's, and
  `eglSwapBuffers` never came back: one core, every other thread parked, six
  compositor frames on the screen and no line in the log between the context
  and the stall report. The runtime now releases the window's buffers both
  ways -- what the suspended presenter left on it before the program's surface
  takes it, and the program's own when the screen goes back -- and says how
  many the presenter's were (`[NXGL] the screen still had 3 buffers registered
  for the compositor: released`, which a run only prints when there were any),
  a refused resize is named
  (`[NXGL] the screen would not take the program's 800x600`), the first three
  swaps are logged with their surface size and how long they took, and a swap
  still inside the driver two seconds later is named in the stall report,
  which also prints the screen's window as libnx keeps it -- slots registered,
  size and format, whether a buffer is out -- so "never registered", "stuck
  waiting for a buffer" and "stuck handing one back" are told apart by reading
  one line. That line now carries the verdict too (`source/nxgl_verdict.h`,
  host-tested by `tests/nxgl_verdict.c`): the swap's surface never held the
  screen, the window is not connected, nothing is registered on it, a buffer is
  out and never came back, the buffers were configured for another size than
  the surface presents at, or none of those and the driver did not return --
  each named with the size, slot count and seconds it was decided on, and the
  size the surface presents at read from the swap itself
  (`wine_nx_gl_swap_surface`, `dlls/win32u/winnx_opengl.c`). A stuck first swap
  used to leave the numbers and no conclusion, which cost a run to read.
  The same reading is now taken while the program is running, twice: when its
  surface has just been made on the window (`the window the program's surface
  was made on: 3 slots, 800x600 format 0, connected, no buffer is out`) and as
  the first swap begins (`the window at the first swap: …`, `nxgl_window_line`
  in `source/nxgl_verdict.h`). A surface whose driver never registered its
  buffers, or a slot already out before the program ever asked, is otherwise
  only visible ten seconds later, when nothing has moved and the cause may have
  been replaced by its consequence.
- **A Vulkan swapchain's buffers are not the runtime's to release.** A Vulkan
  window surface is made for the whole screen and is made again whenever its
  swapchain is (a resize, a format change), and each of those goes through the
  same acquisition. libnx refuses `nwindowSetDimensions` while buffers are
  registered, even a repeat of the size the window already has, and the release
  that follows a refusal then takes the buffers away from the swapchain that is
  about to use them. So an acquisition that reuses the screen a Vulkan surface
  holds no longer asks the window for its size at all, and says so if the window
  is not the size the surface expected
  (`the screen a Vulkan surface reuses is WxH, not WxH`). The order everything
  else goes in -- ask the presenter to step aside, release what its EGL left
  registered, give the window the program's size, then make the surface -- is
  `horizon-wine/tests/check_screen_handover.py`: the real
  `wine_nx_screen_acquire()` and `screen_take_dimensions()` against a mock
  NWindow that behaves as libnx's does, run in CI as "The screen handover".
- **The screen goes back when the last surface does.** A program can hold more
  than one Vulkan surface at a time -- two windows, and the surface of a device
  reset made before the one it replaces is destroyed -- and the runtime kept
  them in one flag. Destroying the older surface therefore handed the screen
  to the compositor, released the window's buffers and left the newer one
  presenting into a screen it no longer had. The holders are counted now, and
  the screen goes back with the last of them (`wine_nx_vulkan_window`,
  `source/runtime.c`).
- **Nothing at the way out waits without an end.** A presenter or an input
  thread stuck in the display driver must not keep the program from closing,
  and one that is asked to hand the screen over must not keep a program's own
  surface off it: the hand-over is bounded at three seconds and the screen is
  taken anyway, with the log naming what did not step aside. A thread that
  would not end leaves its stack lent, so the way out no longer hands the
  process back to the loader when one does not (`wine_nx_compositor_stop` and
  `wine_nx_input_thread_stop` answer, `return_to_launcher` acts on it), and the
  screen's own lock is taken at the end with a bound rather than forever
  (`release_screen_buffers`).
- **The message loop's clock is not free.** Every message loop turn asks for a
  performance counter tick to decide whether to poll the driver, and on Horizon
  a read of that counter is the one system call the runtime deliberately makes
  expensive: a thread it has caught pacing itself is rested 0.1 ms inside the
  read. Two reads per turn -- one to compare, one to store -- therefore cost a
  program a quarter of a millisecond before it looked at its own messages, and
  a program already held back by a refused OpenGL call paid it in the same
  loop. The frequency is now asked for once, the tick comes from a divide by a
  constant, and the check stores the tick it compared against instead of
  reading the clock again.
- **A stall that names the program's own function.** ntdll+0x40130 is where a
  thread is only because it is inside a system call, and every waiting thread
  reports the same thing. A thread stopped there has its x86 context saved in
  its TEB's CPU area, so the report reads the program's own `Eip`, `Esp` and
  `Ebp` out of it (Wine's i386 `CONTEXT`, whose offsets are fixed by winnt.h
  and asserted by `tests/thread_profile.c`) and appends the guest's position
  and its x86 frame chain: `| x86 at 0x4a2b3c DarkAthena.exe+0x1a2b3c < RndrGL.DLL+0x…`.
  The address comes first because an export name is only the nearest one
  below it; the address is what can be checked against a map file.
  The same address is appended to a first-chance fault (`x86=0x…`), so a fault
  in translated code names a function of the program rather than a translated
  address that names nothing. A thread is sampled three times a millisecond
  apart, so a loop shows the places it returns to and not just the one address
  a single sample finds, and the frames that are in the runtime itself -- its
  own code, libnx and Mesa are one image -- are named `runtime+0x…`, which
  separates a thread spinning in the driver from one spinning in translated
  code. Inside one of Wine's own DLLs the module's export
  table turns the offset into a name -- `opengl32.dll!wglSwapBuffers+0x1c`
  instead of `opengl32.dll+0x3db4` -- which is the difference between a thread
  that is somewhere in a megabyte of GL code and one that is in the swap. The
  tables are walked through a window that remembers the mapped piece it last
  asked about, so naming costs one kernel check per piece and cannot fault: an
  image is several pieces with different protections, and asking once whether
  the whole `SizeOfImage` is readable answers no for every one of them -- which
  is exactly how the first version of this named nothing at all.
- **Caches on the card.** Mesa's on-disk shader cache is given a directory of
  its own, `sdmc:/switch/wine/cache`, and a 512 MB cap through
  `MESA_SHADER_CACHE_*` (and the `MESA_GLSL_*` names Mesa 20.1 reads), so a
  game's shaders are compiled once rather than at every start: left alone the
  drivers look under `$HOME/.cache`, which the console does not have. That cap
  covers the multi-file cache only: the single-file one the Vulkan driver fills
  (`mesa_shader_cache_sf/`, a directory per driver version and the driver's own
  below it) does not read `MESA_SHADER_CACHE_MAX_SIZE` at all, so the runtime
  measures it and gives whole driver versions back oldest first at 512 MB,
  keeping 384 MB (`fex_cache_prune_tree`, the measurement the translator's cache
  uses). FEX's translated-code cache (2609's `FEX_DISKCACHE`) is the per-program
  **Translated code cache**, off unless asked for because 2609 calls it
  experimental. Left alone the module keeps it in `%LOCALAPPDATA%\fex-emu`,
  which on the card is the directory the launcher measures and clears; with it
  on, the module is pointed at a directory of its own per build
  (`FEX_DISKCACHEPATH`, from `fex_cache_database_path()`), named after the
  runtime and the CPU module variant, because the release keys its own
  directory by a hash of its settings alone — two builds of the translator
  would read each other's emitted code out of one. The runtime measures the
  cache before the program starts and gives whole configurations back oldest
  first until it is under a gigabyte, keeping 768 MB — the limit 2610 added
  upstream, and the guard 2609 does not have (`source/fex_cache.c`). The cache
  belongs to no one program — the module keys its entries by the code they hold
  — so its size is
  shown, and it is cleared, from the launcher's Settings tab rather than from a
  game's menu; `fex_cache_root()` names the directories both sides use, so the
  runtime and the launcher cannot drift apart. The database is read and written
  plainly (`FEX_DISKCACHEFILEMAPPING=0`): the release maps a fixed, very large
  window onto the file, which on a card is a copy of it page by page. It does
  not go through the card read cache below either: its module looks a block up
  by address, a record or two at a time, so a 128 KB chunk would answer one
  lookup and take a chunk from the game's own data as it went. The runtime
  names the directory with `wine_nx_sd_cache_exclude()`, and files under it are
  read with the card asked for exactly the bytes their reader wanted. A marker
  in `program-settings/` names the start that is under way, so one that never
  comes back — a program that could not start with the cache, a crash — removes
  `fex-cache=1` from that program's settings as it falls back. This keeps the
  cache off for later starts too, until the owner deliberately re-enables it
  (`fex_cache_marker_*`).
- **Two environments, one per kind of library.** The translator's switches
  (`FEX_*`) are read by FEX's own Windows CRT, which takes them from the
  process environment block (`Source/Windows/Common/CRT/Misc.cpp` reads
  `PEB->ProcessParameters->Environment`), so the runtime puts them in the block
  it starts the program with — the same block the program sees. Mesa's are read
  by a Unix library inside this process and have to be `setenv`'d here
  (`mesa_glthread`, the shader-cache names). Neither belongs in the other:
  setting `FEX_*` with `setenv` would say nothing to the module, and a
  `MESA_*` in the block would say nothing to the driver.
- **Audio.** `winenxaudio.drv` plays through audout, for mmdevapi and DirectSound.
- **Input.** The touchscreen, the controller as a mouse or as a keyboard with
  per-game mappings, and XInput, which sees player 1 as an Xbox 360 controller
  by default. A per-game **Controller prompts** choice can report a DualShock 4
  vendor/product ID to titles that select their button artwork from
  `XInputGetCapabilitiesEx`; button data and the XInput API remain unchanged,
  and games that ignore that ID still show Xbox prompts. It is not a DirectInput
  DualShock implementation and does not provide the DS4 touchpad or gyro.
  Mouse-driven games get a recommended layout when they are recognised
  (`source/games/controls.h`): Septerra Core and Harbinger play with the right
  stick moving the mouse, ZL clicking and the right stick's click right-clicking,
  with the keys each game reads beside them. The layout sits between the global
  controls and the player's own per-game changes, so clearing a row in the
  editor brings it back.
- **On-screen keyboard.** `source/osk.c` is a keyboard drawn over the program
  that sends virtual keys one at a time, with scan codes, through the display
  driver (Minus + right stick, or `NtUserShowSoftwareKeyboard` / a text field
  taking focus). Horizon's keyboard applet only returns a finished string, so
  fields a game draws itself could not be edited. Its picture is copied into
  the Vulkan swapchain image before present (`dlls/win32u/vulkan.c`), blitted
  into the back buffer before `eglSwapBuffers` (`winnx_opengl.c`), or drawn as
  the compositor's top layer; the controller is its while it is up.
- **Swapping what the controller is, while a program runs.** Plus and Minus
  (Start and Select) tapped together swap the run between the gamepad and the
  keyboard and mouse the pad stands in for, without going back to the launcher:
  XInput and DirectInput read the same profile, so the pad they offer goes away
  with the swap, and the stick starts moving the arrow. Held instead of tapped,
  the pair still closes the program as it always has -- a second of holding is
  the quit, anything shorter is the tap (`input_chord_step`,
  `source/input_profile.c`), and the log and the run report say
  `[INPUT] + and - tapped: the controller is now ...`. The launcher's Input mode
  is what a run starts on: the swap lasts as long as the run and does not write
  the card, so a game whose menus need a mouse is one tap away without changing
  how it starts. An overlay has the controller while it is up, where the two
  buttons are keys, so the chord waits for it to close.
- **The controls menu, over a running program.** Minus and the left stick click
  open an on-screen display over the game (`source/controls_overlay.c`): a panel
  down the right side of the screen, a section per thing it edits. **Input** is
  what the controller is -- the same swap plus and minus make, and it is written
  to the program's controls as well, so the next run starts that way. **Mapping**
  is the launcher's control rows, each opened into the key or mouse action it
  should make: picked from a grid in keyboard/mouse mode, captured from the
  controller in controller mode. **Mouse** is the stick's cursor speed, by its
  stops. Every change is written to the program's own controls at once, so it
  can be tried the moment the menu closes (B); X clears a row and Y puts the
  game's layout back under it. Left and Right walk the sections, Up and Down the
  rows, ZL and ZR page them. The panel is placed and scaled from the screen it
  is drawn on -- one factor of the smaller side gives its margin, width, rows
  and text sizes, so nothing about it is a fixed number of pixels -- and a tap
  on the console's 1280x720 touch panel is mapped through that same geometry
  into whatever screen the presenters draw for, so a finger lands on what it
  looks like it lands on at every resolution.
- **Memory.** Fixed-base games such as NFSU2 need a 32-bit address space:
  launch the NRO through a forwarder made with "32-bit, no alias", which the
  launcher can install itself and which also raises the memory limit to 2 GiB.
  AMD64 programs use the main 39-bit application forwarder. The same NRO
  selects the execution path from the program's PE header.
  That address space is the ceiling on everything a run does: a Switch keeps
  0.8 GB of its 4 GB for the system, so an application may hold 3.2 GB -- but
  only a 39-bit process can be given it, and a 32-bit one has 4 GB of addresses
  for its code, its heap and every mapping, which is why Horizon reports
  2 GiB to it and `svcSetHeapSize` cannot be given more. Nothing in the runtime
  can raise this: it is set when the process is created, by the forwarder the
  console started. The run says which it is -- `[INIT] processors=3
  memory=2048 MB used=209 MB (32-bit address space: the most this run can
  hold)` -- and it is worth reading before blaming a game for using little RAM:
  a 32-bit program cannot address more than 2 GB of its own, so the memory left
  over can only go to what the runtime keeps in it (the card cache,
  `[PROGRESS] cache_mb=…/…`), not to the game.
  Wine reserves the program's low address space early, keeps its allocations
  clear of the memory Horizon hands to libnx, and leaves 32-bit programs'
  Vulkan memory in the driver's own mappings.
  Within that space Wine gives a 32-bit program the ceiling its own PE header
  asks for: 2 GiB of user addresses unless the image is
  `IMAGE_FILE_LARGE_ADDRESS_AWARE`, 4 GiB when it is
  (`set_large_address_space()`, `dlls/ntdll/unix/virtual.c`; the same bit
  caps `HeapCreate` in `dlls/kernel32/heap.c`). The runtime names the ceiling
  it chose, `[VA] 32-bit program: address space up to ...`, because a program
  that runs out fails a long way from the decision.
  `horizon-wine/tools/large-address-aware.py` reports and sets that bit on a
  game's executable from the host -- the well-known patchers are Windows GUI
  programs, and a card's game is a file on the host's disk anyway. It is not
  a way past the launcher's "Address space unavailable" dialog: that one is
  about the forwarder's own address space, and no bit in a game changes it.
  KUSER_SHARED_DATA, the page Windows keeps at 0x7ffe0000 and every program's
  ntdll reads for the clock, is not that page here: on a 32-bit address space
  the address is inside the pages the kernel gave the process for its heap,
  and the runtime keeps Wine's mappings out of those. The shared data takes a
  page of its own below 4 GiB (`dlls/ntdll/unix/horizon_shared_data.h`,
  `[INIT] the shared data moved off 0x7ffe0000 ...`), and a program reading
  the Windows address directly has the read served from the real page by the
  fault handler (`dlls/ntdll/unix/horizon.c`). A build that adopted the page
  standing at the address instead lost it to the allocator that owned it mid
  load, which parked the clock thread that writes the page and left the
  program with no frames at all.
- **Launcher.** An SDL2 launcher with a Home and a Library view, per-game
  options, a controls editor, SteamGridDB artwork, and forwarder installation
  (`horizon-wine/source/launcher*.c`, `forwarder.c`). Add Game begins with an
  SD Card / USB picker; mounted USB volumes can then be browsed directly.
- **Diagnostics.** A thread and core report, a sampling profiler, fatal fault
  reports that name the x86 instruction behind translated code, and a map of
  the address space when a program runs out of it.

## Files on the card

Everything lives in `sdmc:/switch/wine`: the runtime `wine-nx-runtime.nro`,
Wine's files, and `drive_c` with the programs. C: is `drive_c`, Z: is the
card's root, and mounted USB volumes are D: through H:.

The Windows modules -- `drive_c/windows/system32` and `syswow64`, and the
bundled DXVK and VKD3D-Proton in `drive_c/dxvk`, `dxvk64` and `vkd3d64` -- come
from the DLL repository, not from a release. The launcher reads the
repository's manifest when it starts, and Settings > System > Windows DLLs
downloads what is new or changed (`source/launcher_dlls.c`, over
`source/horizon_dlls.c`): each file into a `.part` beside where it goes, checked
against the manifest's size and SHA-256, then put in place. Starting a game on
a card without them offers them first. The card keeps the manifest it
installed from in `horizon-dlls/manifest.json`, and `horizon-dlls/classes.reg`,
the COM classes its DLLs serve, which the registry loads before `system.reg`. A
file the runtime cannot run (a feature it does not report,
`tools/runtime_features.py`) is left as it is, and one the repository dropped
is removed unless it was changed since. A copy of the repository's `switch`
folder made by hand is an installation like any other. Verify reads every file
on the card and downloads again any that differs.

Runtime settings are one JSON object in `config/settings.json`. The launcher
writes most of them; the rest are for testing. Earlier builds kept each as a
file of its own in the `wine` folder, and those are moved into the JSON file
the first time a newer build starts.

| Key | Default | Meaning (the file it replaced) |
|---|---|---|
| `verbose-log` | false | Wine's traces in the log (`verbose.txt`) |
| `profiler` | false | Thread, server and sampling reports (`profile.txt`) |
| `windows-through-opengl` | true | Windows drawn by the compositor, not the framebuffer (`framebuffer.txt`, inverted) |
| `reopen-the-launcher-on-exit` | false | Start the launcher again when a program ends (`reload-launcher.txt`) |
| `core-balancing` | true | Move busy threads between cores (`no-balance.txt`, inverted) |
| `display-devices` | true | Report display devices (`no-display-devices.txt`, inverted) |
| `gl-debug` | false | The driver's own debug messages in the log, through `GL_KHR_debug` (`gl-debug.txt`) |
| `gl-pinned-buffers-cached` | true | Mesa 20.1 runtime: cached pinned buffers (`gl-uncached.txt`, inverted) |
| `gl-clean-before-submit` | true | Mesa 20.1 runtime (`gl-noclean.txt`, inverted) |
| `run-the-chosen-program` | false | Start `target.txt` directly, without the launcher (`run-entry.txt`) |
| `hand-the-process-back-anyway` | false | Return through the Homebrew loader on exit (`loader-anyway.txt`) |
| `vulkan-probe` | false | Vulkan self-test at startup (`vulkan-probe.txt`) |
| `gl-driver` | `nvc0` | The OpenGL renderer for a program that does not name one: `zink` (Zink over NVK/Vulkan) or `nvc0` (native). A program's own `gl-driver` in its `NAME.wine-nx.txt` overrides it |

The controls every program uses are in `config/keys.txt`. A game Autorun
recognises gets its recommended layout over them (`source/games/controls.h`),
and the program's own controls over that. A program's own files sit next to
its executable, named after it:

| File | Content |
|---|---|
| `NAME.wine-nx.txt` | Title, hidden from the library, verbose traces, profiler, `windows` (compositor or framebuffer), optional `windows-version=winxp` (reports Windows XP SP3 through Wine's version APIs for this launch), optional `windows-locale=cs-CZ` (uses Czech (Czechia), ANSI 1250 and OEM 852 for this launch), the CPU translator and its fast paths, `d3d` and which DXVK or VKD3D-Proton is used, the HUD, the frame limit and vsync, frame generation, `scaling` and upscaling, and dgVoodoo |
| `NAME.args.txt` | Its command-line arguments |
| `NAME.keys.txt` | Its own controls, over `config/keys.txt` |
| `NAME.box64.txt` | Box64 code generation options, one `BOX64_DYNAREC_*=value` per line |

`d3d=dxvk` selects `C:\dxvk` for x86 programs and `C:\dxvk64` for AMD64
programs. Application-local graphics DLLs have priority.

DXVK's own settings are per game and per release: the options screen offers only
what the chosen DXVK parses (`source/dxvk_options.c`), and a value it never set
is not written, so the release's own default stands. Sarek 1.14 is the exception,
because its console tuning does not reach the console: the build applies it for
`deviceID 0x1380` and NVK on Horizon reports the Tegra X1 as **0x0fe0**
(`mesa-switch`, `src/nouveau/horizon/nouveau_horizon_device.c`, passed straight
into `VkPhysicalDeviceProperties.deviceID`), so three shader compiler workers,
low-latency pacing, a 2 GiB budget and the d3d9/d3d11 Tegra defaults would
otherwise never happen. Autorun therefore writes that profile itself for a
setting left at Default -- `Default (3 / Tegra)` and the like in the options
screen -- and a game's own choice always wins. A Sarek build that later
recognises the console is unaffected: the written values are the ones it would
choose. Existing
`d3d9=dxvk` settings remain readable. A game's settings can be set in one go
from a preset (Balanced, Performance, Battery, Compatibility), which writes the
same keys: the preset touches only the translator, the fast paths, vsync and
the frame limit, never the renderer, the upscaler or frame generation. Another
game's settings can be taken whole with Copy settings from
(`launcher_settings_copy()`): everything but the title and whether it is hidden,
which belong to the game itself, and a DXVK or VKD3D-Proton version that is not
installed for the other game's machine is dropped, so the installed one is used.

The launcher's look is kept in `launcher.txt` and its library in
`launcher-library-v2.ini`. Each game there carries the time it has been played,
and a run is timed by the runtime, which is what starts the program and is still
there as it ends: `play-session.txt` says which program is up, when it began and
the last time the runtime said it was still up (written once a minute from the
log flusher's thread), and as the program closes the runtime adds the run to its
game's `play-seconds`. A game started from its forwarder, from `run-next.txt`
behind a fix's setup, or from the command line never meets the launcher and is
counted all the same. A run that ends without the runtime closing it -- the
console switched off, the program killed -- leaves the last sight of it on the
card, and the launcher credits that and no more, so the hours a console slept in
a game are not hours of play; a card that still carries a `started-at` from an
older build is closed by the same settle. A run longer than a day is credited
one day, and a clock that went backwards credits nothing. Every run says in the
log what it added: `[PLAY] doom3.exe ran 41m; 3h 53m played so far`, and the line before
it, `[PLAY] doom3.exe began; … so far`, is what the game carried into the run.

Settings > System > **Back up settings** writes a copy of everything above that
is a setting and not content (`source/launcher_backup.c`), under
`backups/<stamp>/`: the files themselves as `files/NNNN`, and `manifest.txt`
saying where each came from, one `<NNNN>\t<path>` line each. It takes
`launcher.txt`, `keys.txt`, `args.txt` and `config/keys.txt`, the runtime's
`config/settings.json`, every game's own `NAME.wine-nx.txt`, `NAME.args.txt`,
`NAME.keys.txt` and `NAME.box64.txt` beside it, and the `program-settings`
folder that holds the settings of games on USB. Names are the time it was
taken (`2026-10-26-1745`, and `-2` for a second one in the same minute), which
is what the list is sorted by, and the last five are kept and the rest
removed. **Restore settings** copies each file back to the path in its
manifest, making a folder that is gone again, and takes a copy of the settings
it is about to replace first, so a restore can be undone by restoring that
copy. The launcher then re-reads the look, the runtime defaults
(`config/settings.json`) and every game's settings without a restart; the
library, the artwork and the play time are not part of a copy and are never
written by one. A restore that stops at a file (a full card, or a path a
manifest may not name) leaves the files before it back and says how many; the
launcher reads its settings again then too, so what it shows is what the card
holds. A manifest line longer than a path is no line this launcher wrote: the
backup is not listed and not restored rather than being put back with a file
missing from it.

## Logs

Everything the runtime writes is in `switch/wine/logs`. Each run writes
`autorun_runtime.log`, and a copy named after the program: `NAME.log`, with
`_verbose` and `_profiler` added to the name when those were on, so a
diagnostic run keeps its own file. The program's standard handles are
`stdin.txt`, `stdout.txt` and `stderr.txt` there, and verbose runs add
`horizon-trace.log`. `driver.txt` holds the graphics driver's own words: Mesa's
switch backend writes a present that goes wrong to stderr
(`egl-switch present: <call> failed (error=…, slot=…)`), and on this console
stderr goes nowhere else. A log's first lines include `[BUILD]`, the runtime
version, worth
checking before reading anything else. `[PROGRESS]` lines report every 10
seconds: frames, OpenGL and system call rates, memory and translation counters.
Its reading figures tell a slow load apart from a busy one: `reads`/`read_mb`
are what the program asked for, `sd_reads`/`sd_mb` what the card was asked for
after the read cache, and `cache_mb` what that cache holds over `cache_max_mb`,
what it may hold. The cache (`source/sd_read_cache.h`) keeps up to 24 chunks
(3 MB) of each open file, and takes between 32 and 512 MB -- half of the heap a
game leaves free above the 512 MB floor kept for the game itself, so a game
that sits at a sixth of the console's memory hands the card cache hundreds of
megabytes while one that has taken the heap still gets 32 MB. A write to a file
throws away what is held for it. A miss reads a whole 128 KB chunk; reads up to
256 KB go through the cache (a game streaming level data in 128 or 256 KB
pieces used to send every one of them straight to the card, and read the level
again in full the next time it was entered); the cache can also read ahead from
16 KB (`fill_min`), which on The Sims 2 halved the bytes from the card but not
the time. Each thread keeps its read buffer rather than allocating one per
read, and every resize of the pool writes `[CACHE] the card cache went from N
to M MB: … of heap free, … MB held`, so the numbers are in the log without the
profiler. `code_mb` is the translated code the run
holds now over the code memory Horizon gave it, and `code_all_mb` every byte
ever translated: Horizon grants ten code memory objects in all, so translated
code that is never reused is what ends a long run in the interpreter
(`source/box64_code_arena.h`). On a 36- or 39-bit address space the code goes
above 4 GB, out of the program's way; on a 32-bit one it shares the runtime's
window and runs out near 150 MB. There `BOX64_DYNAREC_PURGE=1` in a game's
`NAME.box64.txt` lets blocks it has not entered for `BOX64_DYNAREC_PURGE_AGE`
translations give their room back, at a small cost on every block entry;
`purged=` counts the passes, blocks, megabytes and milliseconds. Freed code
waits two seconds before it is reused.
With the profiler on, `[THREADS]`, `[SERVER]` and `[PROF]` show where each busy
thread spends its time. A frame is named through the module's export table when
it has one (`wow64.dll!…` instead of `wow64.dll+0x…`), and both PE layouts are
read: the ARM64 modules -- Wine's 64-bit half, and ntdll's PE side -- are PE32+,
whose directories sit sixteen bytes further into the optional header than a
32-bit image's, so reading them as PE32 named nothing and every ARM64 frame came
out as `module+0x…` (`source/thread_profile.h`, `tests/thread_profile.c`).
The program's own call chain is read from its frame pointers when it has them,
and otherwise by scanning the words of its stack for return addresses into a
module (`nx_prof_x86_scan`, marked `(scanned)` in the line): a release build has
no frame pointers, and `[STALL] thread 4w … | x86 at 0x6e433db4 … < 0 < 0` was a
spinning thread with its callers left out. In a 32-bit program the scan reads at
most 2 KB and stops at the end of the piece the stack is in. DLLs that fail to load are logged (`[NXLDR]`, and
Wine's `err:` lines) even without verbose traces, and so are files a program
could not open (`[FS]`, with how it asked), the first overlapped socket
operations (`[ASYNC]`), user APCs (`[APC]`) and x86 faults (`[BOX64]`).

## Building

Requirements:

- Docker with the [switch-dev](https://github.com/autorunhq/switch-dev) image
  named in `horizon-wine/switch-dev.txt`, for the Switch build and the Box64
  tests. It is devkitA64 with a newer libnx, mesa-switch (Mesa 26: OpenGL and
  NVK Vulkan), LSFG-VK's Horizon port and libusbhsfs with UASP in portlibs,
  each from a pinned revision.
- LLVM-MinGW 20260505 in `horizon-wine/toolchains/`, fetched as the platform's
  release (`llvm-mingw-20260505-ucrt-macos-universal` on a Mac), and bison
  (Homebrew's), for Wine's PE modules.

Autorun builds the runtime; the Windows modules it runs are the DLL
repository's, [autorun-horizon-dlls](https://github.com/autorunhq/autorun-horizon-dlls),
a submodule at `horizon-dlls/`. That is all of system32 (ARM64X) and syswow64
(i386), FEX's CPU modules, the bundled DXVK and VKD3D-Proton and `classes.reg`,
one set for the x86 and AMD64 runtimes, laid out as on the card and grouped by
the part of Windows each file belongs to. No Autorun package carries any of it.

`sh build.sh` at the repository root does all of it, fetching what is missing
(`--help` lists its options). The steps it runs, from the repository root:

```sh
docker run --rm -v "$PWD:/work" -w /work -e DEVKITPRO=/opt/devkitpro-release "$(cat horizon-wine/switch-dev.txt)" \
    sh horizon-wine/build-boot-bundle.sh         # Atmosphere 1.11.2 and HOC boot payloads, once
WINE_NX_LLVM_MINGW="$PWD/horizon-wine/toolchains/llvm-mingw-20260505-ucrt-macos-universal" \
WINE_NX_FEX=1 WINE_NX_DXVK=1 WINE_NX_VKD3D=1 \
    sh horizon-wine/build-amd64-components.sh    # the AMD64 NRO, and the build-wine-amd64-pe tree
horizon-dlls/tools/build-dlls.py                  # the DLL repository: what changed since its manifest
python3 horizon-wine/tools/package-autorun.py    # the card: autorun-NNN.zip, with the AMD64 runtime
```

The runtime links mesa-switch, LSFG-VK and libusbhsfs from the image's portlibs;
a build records the image's revisions (`switch-dev.json` in the build folder)
in the package's `build-manifest.json`. `WINE_NX_MESA_SWITCH_DIR` links another
mesa-switch build instead, and `WINE_NX_NXVK_DIR`
[nxvk](https://github.com/PalindromicBreadLoaf/nxvk) in place of mesa-switch.
A change to Mesa, LSFG-VK's port or libusbhsfs's UASP patches is made in
switch-dev and comes with a new image.

nxvk is built in its own container, which the switch-dev image does not carry:

```sh
git clone https://github.com/PalindromicBreadLoaf/nxvk && cd nxvk
make image && make && make gl      # Vulkan, then the Zink/Gallium/EGL archives
sudo make install-gl               # libnvk.a, libnvk_support.a, libnvk_gl.a,
                                   # nxvk.pc, nxvk-gl.pc and include/vulkan
```

An installed prefix is what `-DWINE_NX_NXVK_DIR=$DEVKITPRO/portlibs/switch`
takes. A checkout that was built but not installed works too -- the same code
is then still one archive per directory under `switch/build/cross-zink`, which
is where `make gl` leaves it -- and `WINE_NX_NXVK_BUILD` names another build
tree. `cmake/Nxvk.cmake` holds the archive lists, taken from nxvk's own
`Makefile` (`GL_LIBS`, `SUPPORT_LIBS`) and link order, and
`tests/nxvk_archives.cmake` checks them against it without a toolchain
(`cmake -P horizon-wine/tests/nxvk_archives.cmake`).

nxvk's fork-added files (`src/egl/drivers/horizon`, `src/vulkan/wsi/wsi_switch.c`,
`src/nouveau/vulkan/nvkmd/nvgpu`, `switch/`) are GPL-2.0-or-later, and Mesa's
own licenses cover the rest. A runtime linked against its static archives is a
combined GPL work: a package that ships one has to be able to ship its source,
which is why mesa-switch stays the default.

`package-autorun.py` is what a card wants, with the DLL repository's `switch`
folder beside it: the runtime, Wine's NLS files and fonts, Autorun's setup
programs (the components setup, WarCraft III, The Sims 2, Guitar Hero III) and
the default settings, checked against that tree by `verify-package.py`. It
holds no test programs.

The AMD64 runtime runs 32-bit programs too, so it is the only one a card
needs. `build-x86.sh` builds the x86-only runtime (Box64, no FEX) against the
same Wine tree and Mesa, and `package-autorun.py --x86` ships it instead.

`build-dlls.py` builds in `horizon-wine/build-wine-amd64-pe` and rebuilds
only what changed since the commit its manifest names: a Wine module whose
sources, headers, import libraries or build tools changed (as Wine's make
knows them), and a file built from other sources whose recipe changed. After
changing `dlls/winebox64`, `horizon-wine/source/wow64_box64_bridge.c` (which
`winebox64` compiles in), `dlls/wow64` or anything else a module is built from,
commit it and run `build-dlls.py`; `--all` rebuilds everything, for a new
toolchain. Commit and push the DLL repository, then commit the new
`horizon-dlls` here.

The runtime's version is `WINE_NX_RUNTIME_BUILD` in
`horizon-wine/source/runtime.c`, and the archive is written to
`horizon-wine/build-autorun`.

## Tests

```sh
sh horizon-wine/check-runtime-console.sh      # host unit tests of runtime and server pieces, under ASan and UBSan
sh horizon-wine/check-box64-execution.sh      # Box64 interpreter and dynarec in an ARM64 container, plus their Switch build
sh horizon-wine/check-wow64-box64-bridge.sh   # the x86 system-call gate
sh horizon-wine/check-wow64-box64-unix.sh     # the native side of the CPU DLL
sh horizon-wine/check-amd64.sh                # AMD64, ARM64EC and both Box64 CPU modes
sh horizon-wine/check-audio.sh                # the audio driver
sh horizon-wine/tests/check-launcher-host.sh  # the launcher, headless, with scripted input (Homebrew's sdl2, sdl3, sdl2_ttf, libpng)
sh horizon-wine/tests/check-horizon-dlls.sh   # the DLL manager against the horizon-dlls checkout: install, resume, verify, update
sh horizon-wine/tests/check-game-fixes.sh     # game fixes against the game-fixes checkout: match, download, apply, record, update
for t in horizon-wine/tests/check_*.py; do python3 "$t"; done  # server pieces run against real host sockets and files
```

A test whose tool, host, header or checkout is missing prints
`SKIP: needs ...` and exits 0, so a whole sweep runs to the end anywhere.
`.github/workflows/checks.yml` does three things on every push to `main` and
`arena/**`: it compiles every tracked `*.py`, parses every `*.sh` and loads
every `*.json`; it runs the tests that need neither a Switch nor an ARM64 host
(qemu's binfmt handler is registered for the one that needs an ARM64
container); and it builds the lot with `build.sh` in the switch-dev image,
uploading the SD card archive and the log. The GitLab pipeline keeps the jobs
that need a runner with devkitA64.

Box64 is pinned in `horizon-wine/vendor/box64` (fetched by
`tools/bootstrap-box64-core.sh`) and never edited:
`horizon-wine/cmake/Box64Core.cmake` builds patched copies of the files it
changes, and fails if the pinned text moves.

## Layout

| Path | Content |
|---|---|
| `dlls/ntdll/unix/horizon*` | Horizon server, memory, sections, sockets, exception handling |
| `dlls/win32u/winnx_drv.c`, `winnx_vulkan.c` | Display, input and Vulkan surface driver |
| `dlls/winebox64`, `dlls/winebox64ec` | x86 WoW64 and AMD64 ARM64EC CPU DLLs |
| `horizon-wine/source` | Runtime: startup, launcher, compositor, Box64 engine, profiler, audio and XInput backends, forwarder installer |
| `horizon-wine/tests` | Host, PE32 and PE32+ tests |
| `horizon-wine/fex` | The ABI the CPU modules are built against, the patches applied to FEX, and `builder-handover.md`, which is what a release builder is given |

## Limits

- Speed. Heavy Direct3D games are limited by translated x86 code on the game's
  main thread and by Wine's Direct3D layer, rather than by the GPU.
- **One thread at 100% is the program, not a core left unused.** A guest thread
  is translated and run on one core from start to finish: FEX and Box64
  translate the blocks that thread is about to run, on that thread, and there
  is no way to hand half of a single thread's work to another core. What the
  other three cores get is therefore only the work that is not the program's
  own thread -- and the runtime already puts everything it can there:
  `mesa_glthread=true` marshals GL commands onto a worker, DXVK compiles shaders
  on `dxvk.numShaderCompilerThreads` workers (3 in the profile Autorun writes
  for Sarek 1.14) and submits on its own thread, wined3d's CSMT does the same
  for the OpenGL path, the compositor presents on a thread of its own (on core 3
  at priority 63 with the four-core forwarder, `[CORES]`), and Wine's server
  connections, audio and the input poller have theirs. The balancer (`[BALANCE]`, `source/thread_profile.h`)
  keeps the busiest thread's core to itself, light threads included, so the
  thread that is at 100% is not taken off its core a few times a frame. A log
  that shows one core at 100% and the others near idle is that working as
  intended; `[THREADS] one thread carries the program` says so once.
  What does help a single-threaded program is making that one thread do less
  work per frame: the Direct3D path (DXVK over Vulkan rather than wined3d over
  OpenGL), a lower resolution, the **Older OpenGL games** year for
  extension-list reads, FEX's block size (`FEX_MAXINST`) and `FEX_MULTIBLOCK`
  for hot loops, and TSO left off unless a game needs it.
- Wine's first-run setup (wineboot) does not run. The DLL repository writes the COM
  classes its DLLs serve to `horizon-dlls/classes.reg`, and the runtime runs
  `C:\windows\autorun-setup.exe` (`tools/autorun_setup.c`) before the first
  program on a card: the MP3 decoder under Drivers32 and DllRegisterServer for
  the DirectShow and DMO DLLs, which lay out their own filter data. The
  program waits in `run-next.txt` and starts when the runtime starts again; the
  mark is `registry/components-1.done`, so a reset registry runs it again.
- A 32-bit address space leaves a program about 2 GiB of addresses, and Wine
  gives a 32-bit program the 2 GiB its own header asks for unless the image is
  Large Address Aware, which lifts it to 4 GiB.
- AMD64 programs require the 39-bit application forwarder and remain
  experimental.
- One program at a time; one controller.
- Missing DLLs: the card only holds what earlier programs needed. The log names
  what is missing, and the overlay packager builds it.

## Credits

Autorun is built from these projects; each keeps its own copyright and license.

| Project | Authors | License | Used for |
|---|---|---|---|
| [Wine](https://www.winehq.org) | The Wine project authors | LGPL-2.1-or-later | The Windows API, loader, WoW64, and the Direct3D, OpenGL and Vulkan layers; this repository is a Wine fork |
| [Box64](https://github.com/ptitSeb/box64) | ptitSeb and contributors | MIT | x86 and x86-64 execution through its interpreter and ARM64 dynarec (`horizon-wine/vendor/box64`) |
| [DXVK](https://github.com/doitsujin/dxvk) | Philip Rebohle, Joshua Ashton, Robin Kertels, Jeffrey Ellison and contributors | zlib/libpng | Direct3D over Vulkan, for programs set to `d3d=dxvk` |
| [Mesa](https://mesa3d.org) | The Mesa authors | MIT (mostly) | OpenGL through nvc0 and Vulkan through NVK |
| [mesa-switch](https://github.com/danfromtico/mesa-switch) | danfromtico, NaGaa95 and contributors | Mesa's licenses | The Switch port of Mesa 26 (nvc0 and NVK) that the runtime links |
| [nxvk](https://github.com/PalindromicBreadLoaf/nxvk) | PalindromicBreadLoaf | GPL-2.0-or-later for its added files, Mesa's licenses for the rest | An alternative Switch port of Mesa 26 (NVK and Zink over it) the runtime can be linked against |
| Switch ports of Mesa 20.1 and libdrm_nouveau | fincs, Subv, Jules Blok | MIT | The earlier OpenGL path, from devkitPro's packages |
| [libnx](https://github.com/switchbrew/libnx) | switchbrew, libnx authors | ISC | The Horizon system library the runtime is written against |
| [devkitPro](https://devkitpro.org) | devkitPro | Per component | devkitA64 and the Switch builds of the libraries in this table |
| [SDL2 and SDL2_ttf](https://www.libsdl.org) | Sam Lantinga and contributors | zlib | The launcher's drawing, input and text |
| [FreeType](https://freetype.org) | The FreeType Project | FTL or GPL-2.0 | Font rendering in the launcher |
| [HarfBuzz](https://harfbuzz.github.io) | HarfBuzz authors | MIT | Text shaping in the launcher |
| [libpng](http://www.libpng.org), [zlib](https://zlib.net), [bzip2](https://sourceware.org/bzip2/) | Their authors | libpng, zlib, BSD-style | Program icons and compressed data |
| [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) | Martin Storsjö; LLVM and mingw-w64 authors | Apache-2.0 with LLVM exception, mingw-w64's licenses | Building Wine's and DXVK's Windows DLLs |
| [FidelityFX Super Resolution 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) | Advanced Micro Devices | MIT | The Upscaling setting's FSR 1.0 (EASU and RCAS, `tools/fsr1`), which isrmicha brought to the launcher |
| [dolphin-nx](https://github.com/NaGaa95/dolphin-nx) | NaGaa95 | GPL-2.0-or-later | The launcher's look (icon grid, program menu, settings and themes) follows its launcher; Autorun's launcher is its own code |

References that shaped the port without being part of the build:

- [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere): its kernel source is
  how Autorun learns what Horizon's memory calls allow.
- [tico-dolphin](https://github.com/ticohq/tico-dolphin): JIT and exception
  handling on Horizon.
- [WineBox64 NX](https://github.com/Ibnuard/winebox64_nx) by Ibnuard: a proof
  of concept that runs x86-64 Wine itself under Box64 on Horizon. Its Box64 and
  libnx integration was reference material for the execution core; Autorun
  instead runs Wine natively on ARM64, with Box64 as the WoW64 CPU.
- [sphaira](https://github.com/NaGaa95/sphaira) by ITotalJustice and NaGaa95:
  its forwarder code is what the launcher's forwarder installer is ported from.

## More

- [Build-by-build notes](../horizon-wine/README.md)
