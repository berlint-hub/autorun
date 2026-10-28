#!/usr/bin/env python3
"""Where the runtime puts the arrow: the panel pixel it is drawn at, for a
picture the compositor draws and for one it does not.

The arrow is kept in the desktop's coordinates and mapped through the fit in
force every time it is drawn (runtime.c, wine_nx_cursor_panel), so a display
mode, a surface that took the screen, or a change of the Scaling setting moves
it with the picture instead of leaving it where the old fit put it. The stick's
cursor is resized with the desktop at the same time (wine_nx_pointer_rebound),
or it stops a third of the way across a larger screen.

The functions are taken from runtime.c as they are and run against mocks, with
the real screen_fit.h doing the fit; no console, no window system.
"""
from pathlib import Path
import os
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


fixture = r'''
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* runtime.c's own panel size, and the fit it maps through. */
#define WINE_NX_FB_W 1280
#define WINE_NX_FB_H 720

#include "pointer_cursor.h"
#include "screen_fit.h"

/* The desktop, and how the picture is placed on the panel now (runtime.c's
 * own names, so its set-up can be used as it is). */
static int wine_nx_desktop_w = WINE_NX_FB_W, wine_nx_desktop_h = WINE_NX_FB_H;
static int wine_nx_desktop_default_w, wine_nx_desktop_default_h;
static int wine_nx_desktop_wanted_w, wine_nx_desktop_wanted_h;
static int wine_nx_desktop_mode_w, wine_nx_desktop_mode_h;
/* Set while the display shows only the central 4:3 of a surface (the
 * widescreen hack): runtime.c keeps it where the screen is taken. */
static int wine_nx_screen_crop_4_3;
static int scaling_mode = SCREEN_SCALING_FULL;

static void log_line( const char *format, ... ) { }

/* What the runtime's wine_nx_screen_to_panel does: the present's scale and
 * offset, from the same screen_fit_rect the display and the input map use. */
static void wine_nx_screen_to_panel( double x, double y, int *px, int *py )
{
    double scale_x, scale_y;
    int rect_x, rect_y, rect_w, rect_h;
    int dw = wine_nx_desktop_w > 0 ? wine_nx_desktop_w : WINE_NX_FB_W;
    int dh = wine_nx_desktop_h > 0 ? wine_nx_desktop_h : WINE_NX_FB_H;

    screen_fit_rect( scaling_mode, dw, dh, WINE_NX_FB_W, WINE_NX_FB_H, &rect_x, &rect_y, &rect_w, &rect_h );
    scale_x = (double)rect_w / dw;
    scale_y = (double)rect_h / dh;
    if (px) *px = (int)(x * scale_x + rect_x + 0.5);
    if (py) *py = (int)(y * scale_y + rect_y + 0.5);
}

/* The state the runtime keeps around the arrow and the pointer. */
static pthread_mutex_t wine_nx_fb_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct pointer_cursor wine_nx_cursor =
    { .x = WINE_NX_FB_W / 2, .y = WINE_NX_FB_H / 2, .width = WINE_NX_FB_W, .height = WINE_NX_FB_H };
static double wine_nx_cursor_screen_x = WINE_NX_FB_W / 2.0;
static double wine_nx_cursor_screen_y = WINE_NX_FB_H / 2.0;
static int wine_nx_cursor_moved;
static int wine_nx_cursor_visible = 1;

static pthread_mutex_t wine_nx_pointer_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct pointer_cursor wine_nx_pointer;
static int wine_nx_pointer_moved;
static int pointer_setup_calls;

/* What the runtime does with them, recorded instead of done. */
static struct { int x, y, visible; unsigned int calls; } compositor_last;
static unsigned int hint_updates;

static void wine_nx_fb_update_present_hint( void ) { hint_updates++; }
static void wine_nx_compositor_cursor( int x, int y, int visible )
{
    compositor_last.x = x;
    compositor_last.y = y;
    compositor_last.visible = visible;
    compositor_last.calls++;
}
static void wine_nx_pointer_setup( void ) { pointer_setup_calls++; }

/* Forward declarations, as runtime.c has them: the desktop's set-up and the
 * arrow come before the pointer that needs them. */
static void wine_nx_desktop_init( void );
static void wine_nx_pointer_rebound( int width, int height );
static void wine_nx_cursor_move( int x, int y );
static void wine_nx_cursor_place_locked( void );
'''

implementation = "".join(function(runtime, name) for name in
                         ("wine_nx_desktop_init", "wine_nx_desktop_set", "wine_nx_desktop_start_size",
                          "wine_nx_desktop_size", "wine_nx_surface_picture",
                          "wine_nx_cursor_move", "wine_nx_cursor_place_locked",
                          "wine_nx_cursor_panel", "wine_nx_cursor_show", "wine_nx_pointer_rebound"))

checks = r'''
/* The desktop, as the runtime sets it when a program's display mode or its own
 * surface takes the screen. */
static void desktop( int width, int height, int mode )
{
    wine_nx_desktop_w = width;
    wine_nx_desktop_h = height;
    scaling_mode = mode;
}

static void expect_panel( const char *what, double x, double y, int want_x, int want_y )
{
    int px = -1, py = -1;

    wine_nx_cursor_move( (int)x, (int)y );
    assert(wine_nx_cursor_panel( &px, &py ));
    if (px != want_x || py != want_y)
    {
        fprintf( stderr, "FAIL %s: %g,%g mapped to %d,%d, wanted %d,%d\n", what, x, y, px, py, want_x, want_y );
        assert( 0 );
    }
    /* The drawn arrow is that pixel too, and the compositor was told. */
    assert((int)wine_nx_cursor.x == want_x && (int)wine_nx_cursor.y == want_y);
    assert(compositor_last.x == want_x && compositor_last.y == want_y && compositor_last.visible);
}

/* The desktop is the panel: the arrow is where the pointer is. */
static void test_panel_sized_desktop( void )
{
    desktop( 1280, 720, SCREEN_SCALING_FULL );
    expect_panel( "the middle", 640, 360, 640, 360 );
    expect_panel( "the corner", 0, 0, 0, 0 );
    expect_panel( "the far corner", 1279, 719, 1279, 719 );
    expect_panel( "a point", 100, 50, 100, 50 );
}

/* A larger desktop, stretched over the panel: the arrow goes with it. */
static void test_scaled_desktop( void )
{
    desktop( 1920, 1080, SCREEN_SCALING_FULL );
    expect_panel( "1920x1080 middle", 960, 540, 640, 360 );
    expect_panel( "1920x1080 quarter", 480, 270, 320, 180 );
    expect_panel( "1920x1080 corner", 0, 0, 0, 0 );
    expect_panel( "1920x1080 far corner", 1919, 1079, 1279, 719 );

    /* Kept in proportion: a 4:3 desktop has bars at the sides, and the arrow
     * lands inside them, where the picture is. */
    desktop( 800, 600, SCREEN_SCALING_ASPECT );
    expect_panel( "800x600 middle", 400, 300, 640, 360 );
    expect_panel( "800x600 left", 0, 0, 160, 0 );
    expect_panel( "800x600 right", 799, 599, 1119, 719 );
    /* Stretched instead: the same point, the whole panel. Enlarged by 1.6, the
     * panel's last column is between two desktop pixels and nothing can put
     * the arrow on it; 1278 is the last one a desktop point reaches. */
    desktop( 800, 600, SCREEN_SCALING_FULL );
    expect_panel( "800x600 stretched left", 0, 0, 0, 0 );
    expect_panel( "800x600 stretched right", 799, 599, 1278, 719 );
}

/* A picture of another size, or the Scaling setting changed, moves the arrow
 * without the pointer having moved: it is worked out again from the desktop
 * point, by the overlay and by the frame before it is shown. */
static void test_fit_change_moves_a_still_arrow( void )
{
    int px, py;

    desktop( 800, 600, SCREEN_SCALING_ASPECT );
    wine_nx_cursor_move( 200, 300 );
    assert((int)wine_nx_cursor.x == 400 && (int)wine_nx_cursor.y == 360);
    assert(wine_nx_cursor_panel( &px, &py ) && px == 400 && py == 360);

    /* The panel's whole width now, with no pointer move at all. */
    scaling_mode = SCREEN_SCALING_FULL;
    assert(wine_nx_cursor_panel( &px, &py ) && px == 320 && py == 360);
    /* The frame that paints the arrow into the framebuffer asks the same. */
    wine_nx_cursor_place_locked();
    assert((int)wine_nx_cursor.x == 320 && (int)wine_nx_cursor.y == 360);

    /* A program's own display mode: the desktop is another size, so a point
     * that reaches past it is clamped onto the panel, never off it. */
    desktop( 640, 480, SCREEN_SCALING_ASPECT );
    wine_nx_cursor_move( 2000, 2000 );
    assert(wine_nx_cursor_panel( &px, &py ));
    assert(px == WINE_NX_FB_W - 1 && py == WINE_NX_FB_H - 1);
}

/* A program that draws its own cursor: nothing is drawn for it. */
static void test_hidden( void )
{
    int px = -1, py = -1;

    desktop( 1280, 720, SCREEN_SCALING_FULL );
    wine_nx_cursor_show( 0 );
    assert(!wine_nx_cursor_panel( &px, &py ));
    assert(px == -1 && py == -1);
    assert(!compositor_last.visible);
    wine_nx_cursor_show( 1 );
    assert(wine_nx_cursor_panel( &px, &py ));
    assert(compositor_last.visible);
}

/* The desktop changes size under the pointer: it keeps its place in the
 * picture, it can reach the new edges, and the arrow follows it there. */
static void test_pointer_follows_a_new_desktop( void )
{
    struct pointer_cursor before;

    desktop( 1280, 720, SCREEN_SCALING_FULL );
    wine_nx_pointer.width = 1280;
    wine_nx_pointer.height = 720;
    wine_nx_pointer.x = 640;
    wine_nx_pointer.y = 360;
    wine_nx_pointer_moved = 0;
    before = wine_nx_pointer;

    /* A larger desktop: the same place in the picture, and the last pixel is
     * within reach. */
    wine_nx_pointer_rebound( 1920, 1080 );
    assert(wine_nx_pointer.width == 1920 && wine_nx_pointer.height == 1080);
    assert(wine_nx_pointer.x == 960 && wine_nx_pointer.y == 540);
    assert(wine_nx_pointer_moved);   /* the program is told where it now is */
    assert(wine_nx_pointer.x != before.x);
    pointer_cursor_place( &wine_nx_pointer, 99999, 99999 );
    assert(wine_nx_pointer.x == 1919 && wine_nx_pointer.y == 1079);

    /* Back to the panel's own size: proportional again, and the arrow is drawn
     * on the panel's pixels, not the desktop's. */
    desktop( 1280, 720, SCREEN_SCALING_FULL );
    wine_nx_pointer.x = 1919;
    wine_nx_pointer.y = 1079;
    wine_nx_pointer_moved = 0;
    wine_nx_pointer_rebound( 1280, 720 );
    assert(wine_nx_pointer.x == 1279 && wine_nx_pointer.y == 719);
    assert(wine_nx_cursor.x == 1279 && wine_nx_cursor.y == 719);
    assert(compositor_last.x == 1279 && compositor_last.y == 719);
    assert(wine_nx_pointer_moved);

    /* A size that is not a size changes nothing. */
    wine_nx_pointer_rebound( 0, 0 );
    assert(wine_nx_pointer.width == 1280 && wine_nx_pointer.height == 720);
}

/* A program's display mode, set before anything else has run: the set-up that
 * the arrow's own mapping triggers must not put the default desktop back over
 * it.  The Resolution setting is 640x480; the mode is 800x600; and the arrow
 * must be mapped on the mode's desktop, not on the setting's. */
static void test_mode_before_setup( void )
{
    int px = -1, py = -1, width = 0, height = 0;

    wine_nx_desktop_wanted_w = 640;
    wine_nx_desktop_wanted_h = 480;
    wine_nx_desktop_start_size( 640, 480 );
    wine_nx_desktop_set( 800, 600 );

    wine_nx_desktop_size( &width, &height );
    if (width != 800 || height != 600)
    {
        fprintf( stderr, "FAIL the display mode was put back to %dx%d, wanted 800x600\n", width, height );
        assert( 0 );
    }
    /* It is the default the screen goes back to, the mode what it is now. */
    assert(wine_nx_desktop_default_w == 640 && wine_nx_desktop_default_h == 480);
    assert(wine_nx_desktop_mode_w == 800 && wine_nx_desktop_mode_h == 600);
    /* Asking again changes nothing: the set-up runs the once, however many
     * threads get to it first. */
    wine_nx_desktop_init();
    wine_nx_desktop_size( &width, &height );
    assert(width == 800 && height == 600);
    assert(wine_nx_desktop_default_w == 640 && wine_nx_desktop_default_h == 480);

    scaling_mode = SCREEN_SCALING_FULL;
    wine_nx_cursor_move( 400, 300 );
    assert(wine_nx_cursor_panel( &px, &py ));
    if (px != 640 || py != 360)
    {
        fprintf( stderr, "FAIL the arrow maps to %d,%d on the mode's desktop, wanted 640,360\n", px, py );
        assert( 0 );
    }
}

/* Which part of a surface the panel shows: all of it, or the central 4:3 the
 * widescreen hack crops the window to.  The overlay puts the arrow there. */
static void test_surface_picture( void )
{
    int x = -1, y = -1, w = -1, h = -1;

    wine_nx_screen_crop_4_3 = 0;
    assert(wine_nx_surface_picture( 1280, 720, &x, &y, &w, &h ));
    assert(x == 0 && y == 0 && w == 1280 && h == 720);
    assert(wine_nx_surface_picture( 1920, 1080, &x, &y, &w, &h ));
    assert(x == 0 && y == 0 && w == 1920 && h == 1080);

    /* The widescreen hack: 4:3 out of the middle, at the same height, centred,
     * so a 1280x720 buffer shows its 960 middle columns. */
    wine_nx_screen_crop_4_3 = 1;
    assert(wine_nx_surface_picture( 1280, 720, &x, &y, &w, &h ));
    assert(x == 160 && y == 0 && w == 960 && h == 720);
    assert(wine_nx_surface_picture( 1920, 1080, &x, &y, &w, &h ));
    assert(x == 240 && y == 0 && w == 1440 && h == 1080);
    /* A tall buffer: the crop takes the height down and keeps the width. */
    assert(wine_nx_surface_picture( 800, 900, &x, &y, &w, &h ));
    assert(x == 0 && y == 150 && w == 800 && h == 600);

    /* Nothing to speak of. */
    assert(!wine_nx_surface_picture( 0, 720, &x, &y, &w, &h ));
    wine_nx_screen_crop_4_3 = 0;
}

int main(void)
{
    pointer_cursor_place( &wine_nx_pointer, 640, 360 );
    test_panel_sized_desktop();
    test_scaled_desktop();
    test_fit_change_moves_a_still_arrow();
    test_hidden();
    test_pointer_follows_a_new_desktop();
    test_mode_before_setup();
    test_surface_picture();
    assert(pointer_setup_calls > 0);
    puts( "cursor mapping: the arrow follows the fit and the part of the surface the panel shows, "
          "and the pointer follows a new desktop" );
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="autorun-cursor-mapping-") as tmp:
    tmp = Path(tmp)
    for name in ("pointer_cursor.h", "screen_fit.h"):
        (tmp / name).write_text((root / "horizon-wine/source" / name).read_text())
    (tmp / "cursor_mapping.c").write_text(fixture + implementation + checks)
    subprocess.run([os.environ.get("WINE_NX_HOST_CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-fsanitize=address,undefined", "-pthread",
                    str(tmp / "cursor_mapping.c"), "-o", str(tmp / "cursor_mapping"), "-lm"], check=True)
    subprocess.run([str(tmp / "cursor_mapping")], check=True)
print("Cursor mapping: fit, edges, hidden cursor and a desktop that changed size")
