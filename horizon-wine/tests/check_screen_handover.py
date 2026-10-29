#!/usr/bin/env python3
"""Who holds the screen, and in what order the runtime takes it.

Dark Athena's run of 2026-10-29 ended with the program's main thread inside a
GL call, the compositor's last frame on the glass and the screen's window at
"3 slots registered, 800x600 format 0, connected, a buffer is out and has not
come back" -- the compositor had handed the window over a moment before. That
handover is `wine_nx_screen_acquire()` in runtime.c, and the order it does
things in is the whole of the fix: ask the presenter to step aside, take back
what its EGL left registered, then give the window the program's size, and only
then let a surface be made on it.

This runs the real function (and `screen_take_dimensions()`) against a mock
NWindow that behaves as libnx's does -- `nwindowSetDimensions` refuses while
buffers are registered, even for the size the window already has, and
`nwindowReleaseBuffers` is what makes the next call succeed -- and checks both
the result and the order of the calls that got there. No console, no EGL.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
runtime = (root / "horizon-wine/source/runtime.c").read_text()


def function(source, name):
    """The whole definition of a function, from its signature to its last brace.

    runtime.c declares some of them above their use, so the first match can be
    a declaration ending in ';': only one followed by a body is the definition.
    """
    for match in re.finditer(r"^(?:static )?[a-z][\w \*]*\b" + name + r"\(", source, re.M):
        i, depth = match.end() - 1, 0
        while True:
            depth += (source[i] == "(") - (source[i] == ")")
            if depth == 0:
                break
            i += 1
        end = i + 1
        while source[end] in " \t\n":
            end += 1
        if source[end] != "{":
            continue
        depth = 0
        while True:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
            if depth == 0:
                break
        return source[match.start():end] + "\n"
    raise AssertionError(name)


fixture_head = r'''
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* libnx's names, as much of them as this part of runtime.c uses. */
typedef unsigned int u32;
typedef int s32;
typedef unsigned long long u64;
typedef int Result;
typedef struct { int x; } Framebuffer;

#define R_SUCCEEDED(rc) ((rc) == 0)
#define R_FAILED(rc) ((rc) != 0)
#define LIBERRS 0x8001

/* The screen's window, with libnx's own rule in it: a window that has
 * dimensions and buffers registered refuses to be sized at all, and a window
 * with buffers registered refuses even a repeat of the size it has
 * (nx/source/display/native_window.c, nwindowSetDimensions). */
typedef struct
{
    u64 slots_configured;
    s32 cur_slot;
    u32 width, height, format;
    int is_connected;
    s32 crop[4];
} NWindow;

static NWindow nwindow;
static unsigned int releases, dim_calls, crop_calls, framebuffer_closes, suspends;
static const char *order[32];
static unsigned int order_count;

static void record( const char *what )
{
    if (order_count < sizeof(order) / sizeof(order[0])) order[order_count++] = what;
}

static const char *order_text( void )
{
    static char text[256];
    unsigned int i;

    text[0] = 0;
    for (i = 0; i < order_count; i++)
    {
        if (i) strncat( text, " ", sizeof(text) - strlen(text) - 1 );
        strncat( text, order[i], sizeof(text) - strlen(text) - 1 );
    }
    return text;
}

static NWindow *nwindowGetDefault( void ) { return &nwindow; }

static Result nwindowSetDimensions( NWindow *nw, u32 width, u32 height )
{
    dim_calls++;
    record( "set-dimensions" );
    if ((nw->width || nw->height) && nw->slots_configured) return LIBERRS;
    nw->width = width;
    nw->height = height;
    memset( nw->crop, 0, sizeof(nw->crop) );
    return 0;
}

static Result nwindowGetDimensions( NWindow *nw, u32 *width, u32 *height )
{
    *width = nw->width;
    *height = nw->height;
    return 0;
}

static Result nwindowSetCrop( NWindow *nw, s32 left, s32 top, s32 right, s32 bottom )
{
    u32 width, height;
    Result rc;

    crop_calls++;
    record( "set-crop" );
    if (right < left || bottom < top) return LIBERRS;
    if (R_FAILED(rc = nwindowGetDimensions( nw, &width, &height ))) return rc;
    nw->crop[0] = left < 0 ? 0 : left;
    nw->crop[1] = top < 0 ? 0 : top;
    nw->crop[2] = right > (s32)width ? (s32)width : right;
    nw->crop[3] = bottom > (s32)height ? (s32)height : bottom;
    return 0;
}

static Result nwindowReleaseBuffers( NWindow *nw )
{
    releases++;
    record( "release" );
    nw->slots_configured = 0;
    nw->cur_slot = -1;
    return 0;
}

static void framebufferClose( Framebuffer *fb ) { (void)fb; framebuffer_closes++; record( "framebuffer-close" ); }
static void consoleExit( void *ctx ) { (void)ctx; record( "console-exit" ); }
static void wine_nx_fb_update_present_hint( void ) { }

/* runtime.c's own state for this part, under its own names. */
static int wine_nx_console_active;
static Framebuffer wine_nx_fb;
static int wine_nx_fb_ready;
static void *wine_nx_fb_pending_bits;
static int wine_nx_fb_pending_stride, wine_nx_fb_pending_dirty;
static int wine_nx_fb_lock_depth;
static pthread_mutex_t wine_nx_fb_mutex = PTHREAD_MUTEX_INITIALIZER;
static int wine_nx_gl_window, wine_nx_vulkan_window;
static int wine_nx_screen_crop_4_3;
static int wine_nx_compositor_mode = 1;

#define WINE_NX_FB_W 1280
#define WINE_NX_FB_H 720
#define SCREEN_SCALING_ASPECT 0
#define SCREEN_SCALING_FULL 1
#define SCREEN_SCALING_4_3 2
static int wine_nx_scaling_mode = SCREEN_SCALING_FULL;

static int screen_scaling_crops_4_3( int mode ) { return mode == SCREEN_SCALING_4_3; }
static void screen_crop_4_3( int *width, int *height )
{
    int crop_h = *width * 3 / 4;

    if (crop_h <= *height) *height = crop_h;
    else *width = *height * 4 / 3;
}

/* The desktop runtime.c keeps under the surface. */
static int wine_nx_desktop_w = WINE_NX_FB_W, wine_nx_desktop_h = WINE_NX_FB_H;
static int wine_nx_desktop_mode_w, wine_nx_desktop_mode_h;
static void wine_nx_desktop_init( void ) { }
static void wine_nx_pointer_rebound( int w, int h ) { (void)w; (void)h; }

/* The compositor: enabled or not, and whether its presenter really stepped
 * aside. */
static int compositor_enabled = 1, presenter_aside = 1;
static int wine_nx_compositor_enabled( void ) { return compositor_enabled; }
static int wine_nx_compositor_suspend( void ) { suspends++; record( "suspend" ); return presenter_aside; }

/* The log, kept whole: what a run's reader would see. */
static char log_text[8192];
static void log_line( const char *format, ... )
{
    char line[512];
    va_list args;

    va_start( args, format );
    vsnprintf( line, sizeof(line), format, args );
    va_end( args );
    strncat( log_text, line, sizeof(log_text) - strlen(log_text) - 2 );
    strncat( log_text, "\n", sizeof(log_text) - strlen(log_text) - 2 );
}

static int logged( const char *needle ) { return strstr( log_text, needle ) != NULL; }

static int failures;
static void check( int ok, const char *what )
{
    if (!ok) { failures++; fprintf( stderr, "FAIL: %s\n", what ); }
}

static void reset( void )
{
    memset( &nwindow, 0, sizeof(nwindow) );
    nwindow.cur_slot = -1;
    nwindow.is_connected = 1;
    nwindow.format = 0;
    releases = dim_calls = crop_calls = framebuffer_closes = suspends = 0;
    order_count = 0;
    memset( log_text, 0, sizeof(log_text) );
    wine_nx_gl_window = wine_nx_vulkan_window = 0;
    wine_nx_fb_ready = 0;
    wine_nx_fb_lock_depth = 0;
    wine_nx_console_active = 0;
    wine_nx_desktop_w = WINE_NX_FB_W;
    wine_nx_desktop_h = WINE_NX_FB_H;
    wine_nx_scaling_mode = SCREEN_SCALING_FULL;
    compositor_enabled = 1;
    presenter_aside = 1;
}
'''

fixture_tail = r'''
/* The compositor's presenter has given the window up, and left what its EGL
 * registered on it -- one slot out, the frame it had just taken when it was
 * asked to stop. This is the state Dark Athena's program started from. */
static void compositor_with_stale_slots( void )
{
    void *window;

    reset();
    nwindow.width = WINE_NX_FB_W;
    nwindow.height = WINE_NX_FB_H;
    nwindow.slots_configured = 0x7;   /* three of its own */
    nwindow.cur_slot = 1;

    window = wine_nx_screen_acquire( 800, 600, 0 );
    check( window == &nwindow, "the screen is handed to the program's OpenGL" );
    check( suspends == 1, "the presenter was asked to step aside exactly once" );
    check( releases == 1, "the presenter's stale slots were released once" );
    check( nwindow.slots_configured == 0, "the window has no buffers left" );
    check( nwindow.cur_slot == -1, "no buffer is out any more" );
    check( nwindow.width == 800 && nwindow.height == 600, "the window took the program's size" );
    check( !strcmp( order_text(), "suspend release set-dimensions set-crop" ),
           "the release comes before the size, and both before the crop" );
    check( logged( "still had 3 buffers registered for the compositor: released" ),
           "the log says how many were in the way" );
    check( logged( "screen handed to an OpenGL surface" ), "the screen is reported handed over" );
    check( wine_nx_gl_window == 1 && wine_nx_vulkan_window == 0, "the program's OpenGL holds it" );
}

/* A presenter that left nothing behind: the window is clean, and nothing is
 * released for it. A run that releases here is releasing somebody's buffer. */
static void compositor_without_slots( void )
{
    reset();
    nwindow.width = WINE_NX_FB_W;
    nwindow.height = WINE_NX_FB_H;

    check( wine_nx_screen_acquire( 800, 600, 0 ) == &nwindow, "the screen is handed over" );
    check( releases == 0, "nothing was registered, so nothing was released" );
    check( !logged( "registered for the compositor" ), "and the log does not claim there was" );
    check( nwindow.width == 800 && nwindow.height == 600, "the window took the program's size" );
}

/* A presenter that did not step aside in time (it is stuck in the display
 * driver): the screen is taken anyway, and its slots are what
 * screen_take_dimensions has to clear before the window takes the size. */
static void compositor_that_did_not_step_aside( void )
{
    reset();
    presenter_aside = 0;
    nwindow.width = WINE_NX_FB_W;
    nwindow.height = WINE_NX_FB_H;
    nwindow.slots_configured = 0x3;

    check( wine_nx_screen_acquire( 800, 600, 0 ) == &nwindow, "the screen is taken anyway" );
    check( suspends == 1, "it was asked" );
    check( releases >= 1, "its slots had to go for the window to take the size" );
    check( nwindow.width == 800 && nwindow.height == 600, "and the window took it" );
    check( logged( "would not take the program's 800x600" ), "the refusal is named, not silent" );
}

/* The framebuffer was showing windows: it is closed before the surface takes
 * the screen, and the compositor is not asked for anything. */
static void from_the_framebuffer( void )
{
    reset();
    compositor_enabled = 0;
    wine_nx_fb_ready = 1;
    check( wine_nx_screen_acquire( 640, 480, 0 ) == &nwindow, "the screen is handed over" );
    check( framebuffer_closes == 1, "the framebuffer gave the screen up" );
    check( suspends == 0, "and the compositor was not involved" );
    check( nwindow.width == 640 && nwindow.height == 480, "the window took the program's size" );
}

/* A Vulkan surface made for a swapchain the screen already has: the window is
 * the screen's size and the buffers registered on it are the live swapchain's.
 * Asking for the size anyway is refused by libnx, and the release that follows
 * a refusal would take the swapchain's own buffers away. */
static void vulkan_swapchain_reset( void )
{
    reset();
    compositor_enabled = 0;

    check( wine_nx_screen_acquire( 0, 0, 1 ) == &nwindow, "the first Vulkan surface takes the screen" );
    check( nwindow.width == WINE_NX_FB_W && nwindow.height == WINE_NX_FB_H, "at the panel's size" );
    check( wine_nx_vulkan_window == 1, "a Vulkan surface holds it" );

    /* Its swapchain registers its buffers, as the driver's WSI does. */
    nwindow.slots_configured = 0x7;
    nwindow.cur_slot = 2;
    releases = dim_calls = 0;
    order_count = 0;

    check( wine_nx_screen_acquire( 0, 0, 1 ) == &nwindow, "the reset reuses the screen" );
    check( wine_nx_vulkan_window == 2, "and is counted as a second holder" );
    check( releases == 0, "the live swapchain's buffers were left alone" );
    check( dim_calls == 0, "the window was not asked for a size it has" );
    check( nwindow.slots_configured == 0x7 && nwindow.cur_slot == 2, "the buffers are still registered" );
}

/* An OpenGL window while a Vulkan surface holds the screen: refused, and the
 * log says by whom -- not an offscreen surface with a picture nobody sees. */
static void screen_taken_by_vulkan( void )
{
    reset();
    wine_nx_vulkan_window = 1;
    nwindow.width = WINE_NX_FB_W;
    nwindow.height = WINE_NX_FB_H;

    check( wine_nx_screen_acquire( 800, 600, 0 ) == NULL, "no screen for the OpenGL surface" );
    check( logged( "screen busy; OpenGL surface refused" ), "and the log says so" );
    check( releases == 0, "nothing of the Vulkan surface's was touched" );
    check( wine_nx_gl_window == 0, "the OpenGL side does not think it has the screen" );
}

/* The widescreen hack: the crop is only given to a window that took the
 * program's size, and only after it. */
static void widescreen_crop( void )
{
    reset();
    compositor_enabled = 0;
    wine_nx_scaling_mode = SCREEN_SCALING_4_3;
    nwindow.width = WINE_NX_FB_W;
    nwindow.height = WINE_NX_FB_H;

    check( wine_nx_screen_acquire( 1280, 720, 0 ) == &nwindow, "the screen is handed over" );
    check( wine_nx_screen_crop_4_3 == 1, "the central 4:3 is what the display shows" );
    check( nwindow.crop[0] == 160 && nwindow.crop[2] == 1120, "960x720 in the middle of 1280x720" );
    check( !strcmp( order_text(), "set-dimensions set-crop" ),
           "the crop comes after the size" );
    if (strcmp( order_text(), "set-dimensions set-crop" ))
        fprintf( stderr, "  order was: %s\n", order_text() );
}

int main(void)
{
    compositor_with_stale_slots();
    compositor_without_slots();
    compositor_that_did_not_step_aside();
    from_the_framebuffer();
    vulkan_swapchain_reset();
    screen_taken_by_vulkan();
    widescreen_crop();
    if (failures) { fprintf( stderr, "%d check(s) failed\n", failures ); return 1; }
    printf( "screen handover: the order the screen is taken in, and its buffers, passed\n" );
    return 0;
}
'''

source = (fixture_head
          # runtime.c defines the helper after its caller and declares it above;
          # here the definition comes first so the fixture needs no prototype.
          + function(runtime, "screen_take_dimensions")
          + function(runtime, "wine_nx_screen_acquire")
          + fixture_tail)


with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / "screen_handover.c"
    path.write_text(source)
    binary = Path(tmp) / "screen_handover"
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                    "-I", str(root / "horizon-wine/source"), str(path), "-o", str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True)
