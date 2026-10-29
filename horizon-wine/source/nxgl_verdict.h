/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * A screen that shows no frame is one of six things, and the numbers the stall
 * report already collects tell them apart. This is the one place that decides,
 * so the stall line says which and not just how many slots there are: an
 * OpenGL program whose first eglSwapBuffers never returns leaves a log that
 * otherwise ends at "swap #1 began" with no verdict, and the cause can be
 * anywhere from "the drawable never had the screen" to "the driver did not
 * come back".
 *
 * The state is what runtime.c's stall_watch gathers: the time the program's
 * swap has been inside the driver (dlls/win32u/winnx_opengl.c), whether that
 * surface owns the screen, the size it presents at, and the screen's window as
 * libnx keeps it (slots registered, a buffer out or not, connected or not, and
 * the size its buffers were configured at).
 *
 * Shared by the runtime and its host test (horizon-wine/tests/nxgl_verdict.c).
 */
#ifndef WINE_NX_NXGL_VERDICT_H
#define WINE_NX_NXGL_VERDICT_H

#include <stddef.h>
#include <stdio.h>

/* A swap inside the driver for less than this is slow, not stuck; the same
 * bound the stall report used before it said anything about a swap. */
#define NXGL_SWAP_STUCK_MIN_MS 2000

/* Who holds the screen while the swap is open. The stall watch reads the two
 * presenter flags and never asks the compositor -- its state is behind a lock a
 * stuck presenter may hold -- so NXGL_OWNER_COMPOSITOR is only named in
 * messages, by a caller that has the lock. */
enum nxgl_owner
{
    NXGL_OWNER_NONE = 0,   /* nobody the report can see: framebuffer, console, compositor */
    NXGL_OWNER_GL,         /* the program's own OpenGL surface */
    NXGL_OWNER_VULKAN,     /* a Vulkan surface (DXVK's presets, VKD3D) */
    NXGL_OWNER_COMPOSITOR, /* the launcher's compositor (see above) */
};

enum nxgl_verdict
{
    NXGL_SWAP_NOT_STUCK = 0, /* no swap has been inside the driver long enough */
    NXGL_SWAP_NO_SCREEN,     /* the surface that is swapping does not hold the screen now */
    NXGL_SWAP_NOT_CONNECTED, /* the screen's window is not connected */
    NXGL_SWAP_NO_SLOTS,      /* nothing is registered on the window at all */
    NXGL_SWAP_SLOT_OUT,      /* a buffer is out and the display never returned it */
    NXGL_SWAP_OTHER_SIZE,    /* the slots were configured for another size */
    NXGL_SWAP_DRIVER,        /* the window looks right: the driver did not return */
};

struct nxgl_swap_state
{
    unsigned int stuck_ms;     /* the program's swap inside the driver, 0 if none */
    int owner;                 /* enum nxgl_owner */
    int surface_has_screen;    /* the swap began on a surface that had the screen then */
    int surface_w, surface_h;  /* the size that surface presents at, 0 if unknown */
    int window_w, window_h;    /* the screen's window as libnx has it */
    unsigned int slots;        /* slots_configured, counted */
    int slot_out;              /* a buffer is out of the window (cur_slot >= 0) */
    int connected;             /* the display is connected */
};

static inline const char *nxgl_owner_name( int owner )
{
    switch (owner)
    {
        case NXGL_OWNER_GL: return "the program's OpenGL";
        case NXGL_OWNER_VULKAN: return "a Vulkan surface";
        case NXGL_OWNER_COMPOSITOR: return "the compositor";
        default: return "nothing";
    }
}

/* Why no frame is coming, from the state above. The order is the order a
 * reader would check them: where the picture should go before whether it can
 * go there, and the window's own books before blaming the driver. */
static inline enum nxgl_verdict nxgl_swap_verdict( const struct nxgl_swap_state *s )
{
    if (s->stuck_ms < NXGL_SWAP_STUCK_MIN_MS) return NXGL_SWAP_NOT_STUCK;
    /* A swap is published only by a drawable that had the screen when it
     * began (a pbuffer's returns without presenting, and one whose screen a
     * Vulkan surface took stands down), so that flag alone would never fire:
     * what decides this is who holds the screen now. The screen going to
     * Vulkan or back to the compositor with a swap open leaves that present
     * with nowhere to go, which is a different fix from the driver hanging. */
    if (!s->surface_has_screen || s->owner != NXGL_OWNER_GL) return NXGL_SWAP_NO_SCREEN;
    if (!s->connected) return NXGL_SWAP_NOT_CONNECTED;
    if (!s->slots) return NXGL_SWAP_NO_SLOTS;
    if (s->slot_out) return NXGL_SWAP_SLOT_OUT;
    /* Both sides have to be known before they can disagree: a size a query did
     * not answer is not a mismatch, and a verdict built on it would send the
     * next run looking for the wrong thing. */
    if (s->surface_w > 0 && s->surface_h > 0 && s->window_w > 0 && s->window_h > 0 &&
        (s->surface_w != s->window_w || s->surface_h != s->window_h))
        return NXGL_SWAP_OTHER_SIZE;
    return NXGL_SWAP_DRIVER;
}

/* One line for the log, or 0 when there is nothing to say. It names the cause
 * and the numbers behind it, so the next run can be acted on without a second
 * round trip; a line that only says "stuck" costs another run to read. */
static inline int nxgl_swap_report( char *buffer, size_t size, const struct nxgl_swap_state *s )
{
    enum nxgl_verdict verdict = nxgl_swap_verdict( s );
    unsigned int whole = s->stuck_ms / 1000, tenth = s->stuck_ms % 1000 / 100;

    if (verdict == NXGL_SWAP_NOT_STUCK) { if (size) buffer[0] = 0; return 0; }
    switch (verdict)
    {
    case NXGL_SWAP_NO_SCREEN:
        if (!s->surface_has_screen)
            snprintf( buffer, size, "the program's swap has been open for %u.%u s on a surface that has "
                      "no screen (%s holds it): the picture is drawn offscreen and nothing will present it",
                      whole, tenth, nxgl_owner_name( s->owner ) );
        else if (s->owner == NXGL_OWNER_NONE)
            snprintf( buffer, size, "the program's swap has been open for %u.%u s and the screen has been "
                      "given back since it began: no present can finish", whole, tenth );
        else
            snprintf( buffer, size, "the program's swap has been open for %u.%u s and the screen went to "
                      "%s after it began: no present can finish", whole, tenth,
                      nxgl_owner_name( s->owner ) );
        break;
    case NXGL_SWAP_NOT_CONNECTED:
        snprintf( buffer, size, "the screen's window is not connected: nothing the program queues there "
                  "can be shown, and the swap has waited %u.%u s", whole, tenth );
        break;
    case NXGL_SWAP_NO_SLOTS:
        snprintf( buffer, size, "the program's swap has been inside the driver for %u.%u s and the screen "
                  "has no buffers registered by any client: the surface's EGL buffers never reached the "
                  "window (%dx%d, %s)", whole, tenth, s->window_w, s->window_h,
                  nxgl_owner_name( s->owner ) );
        break;
    case NXGL_SWAP_SLOT_OUT:
        snprintf( buffer, size, "the program's swap has been inside the driver for %u.%u s and a buffer is "
                  "out of the screen and has not come back: the display never returned it (%u slots "
                  "registered)", whole, tenth, s->slots );
        break;
    case NXGL_SWAP_OTHER_SIZE:
        snprintf( buffer, size, "the program's swap has been inside the driver for %u.%u s and the screen's "
                  "buffers are %dx%d while the surface presents %dx%d: they were configured for another "
                  "surface", whole, tenth, s->window_w, s->window_h, s->surface_w, s->surface_h );
        break;
    default:
        snprintf( buffer, size, "the program's swap has been inside the driver for %u.%u s: the screen is "
                  "%dx%d with %u slots registered and none out, so the driver itself did not return from "
                  "the present", whole, tenth, s->window_w, s->window_h, s->slots );
        break;
    }
    return 1;
}

/* The screen's window as libnx keeps it, in one line of its own: printed by the
 * display driver when a surface has just taken the window (dlls/win32u/
 * winnx_opengl.c) and before the program's first swap. The stall report has
 * always printed the same numbers when nothing is moving; this is the same
 * reading taken while something is, which is what tells "the driver never
 * registered the program's buffers" from "it did and one is still out". */
struct nxgl_window_state
{
    unsigned int slots;       /* slots_configured, counted */
    int width, height;        /* the window's own size */
    unsigned int format;
    int connected;
    int slot_out;             /* cur_slot >= 0 */
};

static inline int nxgl_window_line( char *buffer, size_t size, const struct nxgl_window_state *w )
{
    if (!size) return 0;
    snprintf( buffer, size, "%u slot%s, %dx%d format %u, %s, %s",
              w->slots, w->slots == 1 ? "" : "s", w->width, w->height, w->format,
              w->connected ? "connected" : "not connected",
              w->slot_out ? "a buffer is out and has not come back" : "no buffer is out" );
    return 1;
}

#endif
