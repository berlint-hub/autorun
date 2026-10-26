/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * The bridge from a running game to the panel rate: SaltyNX's SaltySD
 * sysmodule can set the Switch panel to 40-60 Hz handheld and to 40-120 Hz
 * docked, and FPSLocker asks it to through the shared block the plugin and
 * NX-FPS use. Autorun measures how much work a frame is (see
 * display_sync.h and the note below) and writes the rate the panel should
 * follow into FPSlocked, which is the field FPSLocker reads as the wanted
 * refresh rate. Nothing here changes the swap interval, drops a present or
 * touches a semaphore: the panel does the pacing.
 *
 * The numbers and the protocol are NetherSX2_nx's, which has run this on the
 * console against SaltyNX/FPSLocker: NethFPS port first, legacy SaltySD second,
 * shared block found by NX_FPS_MAGIC, and GetDisplayRefreshRate for the rate.
 *
 * Why a port of its own matters: SaltySD's legacy endpoint serves one session
 * and SaltyNX Core holds it, so a second client gets OutOfResource
 * (0x00010801). That is why NetherSX2 needed a SaltyNX build with a second
 * named port, and the same is true here.
 */
#ifndef WINE_NX_SALTY_DISPLAY_H
#define WINE_NX_SALTY_DISPLAY_H

#include <stdint.h>

/* 1 asks the panel to follow the game; 0 hands FPSlocked back to whoever had
 * it. Safe to call before anything is mapped, and with no SaltySD there at
 * all -- then it only remembers the request and logs once. */
void wine_nx_display_sync_configure(int enabled);

/* One frame's own work: the time between the last present returning and this
 * frame reaching its present, with no wait for the display in it. Called from
 * the present path, so it allocates nothing, locks nothing and never blocks. */
void wine_nx_display_sync_note_frame(uint64_t busy_ns);

/* 1 while the panel is being asked to follow the game. */
int wine_nx_display_sync_active(void);

/* What the last step decided, for the runtime log: the measured rate, the rate
 * asked for, and why (0 nothing yet, 1 measured, 2 published, 3 FPSLocker owns
 * the target, 4 no shared block, 5 no SaltySD endpoint). */
struct wine_nx_display_sync_report
{
    unsigned measured_fps;
    unsigned panel_rate;
    unsigned service_rate;
    int status;
};

void wine_nx_display_sync_report(struct wine_nx_display_sync_report *report);

/* The program is ending: the thread stops, the shared block is closed and
 * FPSlocked is left as it was found. */
void wine_nx_display_sync_shutdown(void);

#ifdef WINE_NX_SALTY_TEST
/* One pass of what the bridge's thread does, for tests/salty_display.c: the
 * host has no scheduler and moves the clock by hand. Never built into the
 * runtime. */
void wine_nx_display_sync_test_pass(void);
#endif

#endif
