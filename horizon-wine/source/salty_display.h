/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * The bridge from a running game to the panel rate: SaltyNX's SaltySD
 * sysmodule can set the Switch panel to 40-60 Hz handheld and to 40-120 Hz
 * docked, and FPSLocker asks it to through the shared block the plugin and
 * NX-FPS use. Autorun measures how much work a frame is (see
 * display_sync.h and the note below) and writes the rate the panel should
 * follow into the block's target byte -- FPSlocked in handheld, FPSlockedDocked
 * docked, since SaltyNX reads one or the other by mode and would ignore a rate
 * put in the wrong one. Nothing here changes the swap interval, drops a present
 * or touches a semaphore: the panel does the pacing.
 *
 * Two things on the console decide whether that rate is applied, and neither is
 * this program's to set:
 *   - SaltyNX only applies it with FPSLocker's display sync on
 *     (sdmc:/SaltySD/flags/displaysync.flag, displaysyncdocked.flag docked).
 *     Without it the byte is written and the panel stays where it is; the
 *     bridge notices after three seconds and says so in the log, once.
 *   - Which of the two bytes is used depends on the mode, and the mode is asked
 *     of omm ("operation mode manager") once a second -- the applet value libnx
 *     caches only follows an applet message, and this program's loop is Wine's,
 *     so it would miss a dock in the middle of a game. Without omm the cached
 *     value is the fallback, and a panel above 60 Hz means docked.
 *   - FPSLocker loads its saved target for the title from
 *     sdmc:/SaltySD/plugins/FPSLocker/<titleid>.dat when the game starts, and
 *     that value lands in the block after this bridge's own write. A saved
 *     target therefore wins for the session, which is the right way round: the
 *     player's choice over the automatic one.
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

/* 1 asks the panel to follow the game; 0 hands the target bytes back to whoever
 * had them. Safe to call before anything is mapped, and with no SaltySD there at
 * all -- then it only remembers the request and logs once. */
void wine_nx_display_sync_configure(int enabled);

/* One frame's own work: the time between the last present returning and this
 * frame reaching its present, with no wait for the display in it. Called from
 * the present path, so it allocates nothing, locks nothing and never blocks. */
void wine_nx_display_sync_note_frame(uint64_t busy_ns);

/* 1 while the panel is being asked to follow the game. */
int wine_nx_display_sync_active(void);

/* What the last step decided. */
enum
{
    WINE_NX_DISPLAY_SYNC_NOTHING = 0,    /* the bridge is off, or nothing has run yet */
    WINE_NX_DISPLAY_SYNC_MEASURING,      /* too few frames, or the game has not drawn */
    WINE_NX_DISPLAY_SYNC_FOLLOWING,      /* the panel is at the rate the game reaches */
    WINE_NX_DISPLAY_SYNC_FPSLOCKER,      /* FPSLocker's own target is in the block */
    WINE_NX_DISPLAY_SYNC_NO_BLOCK,       /* the shared memory has no FPSLocker block */
    WINE_NX_DISPLAY_SYNC_NO_ENDPOINT,    /* no SaltySD to ask through */
};

/* The measured rate, the rate asked for, the rate the service reports, and what
 * the last step decided. Read by the runtime as the game ends. */
struct wine_nx_display_sync_report
{
    unsigned measured_fps;
    unsigned panel_rate;
    unsigned service_rate;
    int status;
};

void wine_nx_display_sync_report(struct wine_nx_display_sync_report *report);

/* The status as a sentence for the runtime log. Never NULL. */
const char *wine_nx_display_sync_status_text(int status);

/* The program is ending: the thread is waited for and closed, the shared block
 * is closed and both target bytes are left as they were found. A game that
 * crashes leaves its last rate in the block; the next session then finds a value
 * it did not write and leaves it to FPSLocker, which is the safe side of that
 * (and FPSLocker's own .dat reload usually overwrites it anyway). */
void wine_nx_display_sync_shutdown(void);

#ifdef WINE_NX_SALTY_TEST
/* One pass of what the bridge's thread does, for tests/salty_display.c: the
 * host has no scheduler and moves the clock by hand. Never built into the
 * runtime. */
void wine_nx_display_sync_test_pass(void);
#endif

#endif
