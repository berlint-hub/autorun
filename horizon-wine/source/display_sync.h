/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * Asking the panel to run at the rate a game actually draws at, instead of
 * capping the game to the panel: a game that holds 47 fps is shown on a 45 Hz
 * panel rather than 47 frames judged against 60. libnx's VI service cannot do
 * this -- it reports resolution and owns no refresh-rate query at all -- but
 * SaltyNX's SaltySD sysmodule can, over IPC. On the handheld panel it drives
 * 40 to 60 Hz in 5 Hz steps by rewriting the DSI timings (a ~78 MHz pixel
 * clock); docked it has real CVT-RBv2 and CEA-861 timings up to 120 Hz.
 *
 * This header is the rate policy alone, shared by the runtime and its host test
 * (horizon-wine/tests/display_sync.c). The numbers are the ones NetherSX2_nx
 * validated on this console against SaltyNX/FPSLocker: 64 present intervals,
 * 60 of them before anything is published, a rate that rounds upward (49 fps
 * asks for 50 Hz), one 5 Hz step at a time, and two seconds of stillness before
 * a step is taken. Its bridge publishes only the handheld target -- it never
 * writes the docked one, because it does not validate a docked table -- and
 * FPSLocker, which owns the display-sync toggle, is what acts on it.
 *
 * Autorun cannot reuse that bridge as it stands. SaltySD's legacy port is a
 * one-session service and SaltyNX Core holds the session, so a second client
 * gets OutOfResource (0x00010801); NetherSX2 needed a SaltyNX fork with a second
 * named port for exactly this reason. And the measurement differs: NetherSX2's
 * emulator core has a frame rate of its own independent of the panel, while a
 * Wine game under FIFO is paced by the panel itself, so its presents report the
 * panel rate and never the rate the game could reach. What to measure here is
 * the runtime's own pacer slack (time not spent waiting), not the present
 * cadence. That choice belongs to the caller; this header only turns a measured
 * rate into a panel request.
 */
#ifndef WINE_NX_DISPLAY_SYNC_H
#define WINE_NX_DISPLAY_SYNC_H

#include <stddef.h>
#include <stdint.h>

/* The handheld rates this policy asks for. SaltyNX's handheld table starts at
 * 40 Hz; the bridge that was validated on the console stays at 45 and above,
 * so an under-run does not ask the panel for something no one has run. */
#define WINE_NX_DISPLAY_SYNC_MIN_RATE 45u
#define WINE_NX_DISPLAY_SYNC_MAX_RATE 60u
#define WINE_NX_DISPLAY_SYNC_STEP 5u
/* Presentation intervals kept, and how many are needed before a rate is
 * published at all: a game's first frames are load screens and shader
 * compilation. */
#define WINE_NX_DISPLAY_SYNC_SAMPLES 64u
#define WINE_NX_DISPLAY_SYNC_MIN_SAMPLES 60u
/* A step is held this long before it is taken. */
#define WINE_NX_DISPLAY_SYNC_STABLE_MS 2000u
/* Rates outside this are not a frame rate: a hitch is not 4 fps and a broken
 * timer reading is not 900. */
#define WINE_NX_DISPLAY_SYNC_MIN_FPS 14u
#define WINE_NX_DISPLAY_SYNC_MAX_FPS 120u

struct wine_nx_display_sync_estimate
{
    unsigned average_fps;   /* every interval in the window, so a hitch shows */
    unsigned upper_fps;     /* reciprocal of the 10th-percentile interval */
};

/* The smallest rate the panel may be asked for that can still show fps.
 * Rounds upward: 49 fps on a 45 Hz panel would drop a frame every nine. */
static inline unsigned wine_nx_display_sync_legal_rate( unsigned fps )
{
    if (!fps) return 0;
    if (fps <= 45) return 45;
    if (fps <= 50) return 50;
    if (fps <= 55) return 55;
    return 60;
}

/* The next rate to ask for, one legal step at a time. A down step needs the
 * measured rate to have fallen a whole step below the current one, so a game
 * hovering at 58 does not put the panel down to 55 and back. */
static inline unsigned wine_nx_display_sync_next_rate( unsigned current, unsigned measured_fps )
{
    unsigned wanted = wine_nx_display_sync_legal_rate( measured_fps );

    if (!wanted) return current;
    if (!current || current < WINE_NX_DISPLAY_SYNC_MIN_RATE ||
        current > WINE_NX_DISPLAY_SYNC_MAX_RATE || current % WINE_NX_DISPLAY_SYNC_STEP)
        return wanted;
    if (wanted > current) return current + WINE_NX_DISPLAY_SYNC_STEP;
    if (wanted < current && measured_fps + WINE_NX_DISPLAY_SYNC_STEP <= current)
        return current - WINE_NX_DISPLAY_SYNC_STEP;
    return current;
}

/* 1 once target has been the same for stable_ticks. The caller owns the state,
 * which is what makes this testable without a clock. */
static inline int wine_nx_display_sync_stable( unsigned target, unsigned *candidate,
                                               uint64_t *candidate_since, uint64_t now_ticks,
                                               uint64_t stable_ticks )
{
    if (!target || !candidate || !candidate_since) return 0;
    if (*candidate != target || !*candidate_since || now_ticks < *candidate_since)
    {
        *candidate = target;
        *candidate_since = now_ticks;
        return 0;
    }
    return now_ticks - *candidate_since >= stable_ticks;
}

/* Rate of the last count presentation intervals, measured in ticks of
 * ticks_per_second. The upper rate is the one the panel follows: a single long
 * load hitch would otherwise drag the whole request down, while the average is
 * still reported next to it so the log can say both. */
static inline struct wine_nx_display_sync_estimate
wine_nx_display_sync_estimate( const uint64_t *intervals, size_t count, uint64_t ticks_per_second,
                               size_t minimum_samples )
{
    struct wine_nx_display_sync_estimate result = { 0, 0 };
    uint64_t sorted[WINE_NX_DISPLAY_SYNC_SAMPLES], total = 0;
    size_t i;

    if (!intervals || !ticks_per_second || !count || count < minimum_samples || !minimum_samples)
        return result;
    if (count > WINE_NX_DISPLAY_SYNC_SAMPLES) count = WINE_NX_DISPLAY_SYNC_SAMPLES;
    for (i = 0; i < count; i++)
    {
        uint64_t value = intervals[i];
        size_t at = i;

        if (!value) return result;   /* no interval is no measurement */
        total += value;
        while (at && sorted[at - 1] > value)
        {
            sorted[at] = sorted[at - 1];
            at--;
        }
        sorted[at] = value;
    }

    {
        uint64_t average = (ticks_per_second * count + total / 2) / total;
        uint64_t upper = (ticks_per_second + sorted[(count - 1) / 10] / 2) / sorted[(count - 1) / 10];

        if (average >= WINE_NX_DISPLAY_SYNC_MIN_FPS && average <= WINE_NX_DISPLAY_SYNC_MAX_FPS)
            result.average_fps = (unsigned)average;
        if (upper >= WINE_NX_DISPLAY_SYNC_MIN_FPS && upper <= WINE_NX_DISPLAY_SYNC_MAX_FPS)
            result.upper_fps = (unsigned)upper;
    }
    return result;
}

#endif
