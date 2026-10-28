/*
 * The arrow's runs over a picture the compositor does not draw.
 * (horizon-wine/source/pointer_overlay.h)
 *
 * A blit or a buffer-image copy writes whole pixels and does not blend, so the
 * arrow goes into a program's own picture as rectangles of opaque pixels. What
 * matters here is that they cover the arrow and nothing else: every pixel the
 * arrow means to colour is in exactly one run, no pixel it does not is in any,
 * and what a run copies is the arrow the picture shows there -- at the panel's
 * size, at a picture scaled to 1080p, and at one smaller than the panel, on the
 * edges of the picture and at its corners.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pointer_overlay.h"

#define PANEL_W 1280
#define PANEL_H 720

static int failures;

static void fail( const char *what, int x, int y, int value, int run )
{
    if (failures++ < 10)
        fprintf( stderr, "FAIL %s: %d,%d - %d,%d\n", what, x, y, value, run );
}

/* The picture the panel shows, in the surface's pixels: the test's own, set by
 * check_runs (the whole surface unless it says otherwise). */
static int picture_x, picture_y, picture_w, picture_h;

/* What the picture shows at a surface pixel: the sprite pixel there, or -1
 * when that pixel is not the arrow's. */
static int sampled_sprite( int x, int y, int px, int py )
{
    int column, row;

    /* Outside the rectangle the panel shows: the arrow is not there, whatever
     * the surface has in that part of the buffer. */
    if (x < picture_x || x >= picture_x + picture_w || y < picture_y || y >= picture_y + picture_h) return -1;
    column = (int)(((long long)(x - picture_x) * 2 + 1) * PANEL_W / ((long long)picture_w * 2)) - px;
    row = (int)(((long long)(y - picture_y) * 2 + 1) * PANEL_H / ((long long)picture_h * 2)) - py;

    if (column < 0 || column >= POINTER_CURSOR_W || row < 0 || row >= POINTER_CURSOR_H) return -1;
    if (pointer_cursor_shape[row][column] == '.') return -1;
    return row * POINTER_CURSOR_W + column;
}

/* Every pixel of the arrow in exactly one run, and every other pixel in none,
 * with the runs' sources saying what the copy puts there. */
static void check_picture( const char *what, int px, int py, int sw, int sh,
                           int x, int y, int w, int h )
{
    struct pointer_overlay_run runs[POINTER_OVERLAY_MAX_RUNS];
    static signed char *covered;
    int count, i, x2, y2, arrow = 0;

    picture_x = x;
    picture_y = y;
    picture_w = w;
    picture_h = h;
    count = pointer_overlay_runs( px, py, PANEL_W, PANEL_H, x, y, w, h, runs );

    if (count < 1) fail( what, px, py, count, 0 );
    if (!covered) covered = malloc( 2560 * 1440 );
    if (!covered) return;
    memset( covered, 0, (size_t)sw * sh );
    for (i = 0; i < count; i++)
        for (y2 = runs[i].y0; y2 < runs[i].y1; y2++)
            for (x2 = runs[i].x0; x2 < runs[i].x1; x2++)
            {
                if (x2 < 0 || x2 >= sw || y2 < 0 || y2 >= sh || covered[y2 * sw + x2]++)
                    fail( what, x2, y2, i, 1 );
            }

    for (y2 = 0; y2 < sh; y2++)
        for (x2 = 0; x2 < sw; x2++)
        {
            int sprite = sampled_sprite( x2, y2, px, py );

            if ((sprite >= 0) != (covered[y2 * sw + x2] == 1)) fail( what, x2, y2, sprite, covered[y2 * sw + x2] );
            if (sprite >= 0) arrow++;
        }
    if (!arrow) fail( what, px, py, 0, 2 );

    /* A run's source is the sprite pixels its destination shows, from its top
     * row and left column on. */
    for (i = 0; i < count; i++)
    {
        const struct pointer_overlay_run *run = &runs[i];
        int first = sampled_sprite( run->x0, run->y0, px, py );
        int last = sampled_sprite( run->x1 - 1, run->y1 - 1, px, py );

        if (first < 0 || run->src_y != first / POINTER_CURSOR_W || run->src_x0 != first % POINTER_CURSOR_W)
            fail( what, run->x0, run->y0, first, i );
        if (last < 0 || last / POINTER_CURSOR_W != run->src_y ||
            last % POINTER_CURSOR_W != run->src_x1 - 1)
            fail( what, run->x1 - 1, run->y1 - 1, last, i );
        if (run->src_x1 - run->src_x0 > run->x1 - run->x0) fail( what, run->x0, run->y0, run->x1 - run->x0, i );
    }

    /* And what a copy puts in a run's pixels is the arrow's colour there, never
     * the clear of a '.'. */
    for (i = 0; i < count; i++)
    {
        const struct pointer_overlay_run *run = &runs[i];

        for (x2 = 0; x2 < run->x1 - run->x0; x2++)
        {
            uint32_t pixel = pointer_overlay_pixel( run, x2 );
            int sprite = sampled_sprite( run->x0 + x2, run->y0, px, py );

            if (pixel != 0xffffffffu && pixel != 0xff000000u) fail( what, run->x0 + x2, run->y0, (int)pixel, i );
            if (w == PANEL_W && h == PANEL_H && sprite >= 0 &&
                pixel != (pointer_cursor_shape[sprite / POINTER_CURSOR_W][sprite % POINTER_CURSOR_W] == 'W' ?
                          0xffffffffu : 0xff000000u))
                fail( what, run->x0 + x2, run->y0, (int)pixel, i );
        }
    }
}

/* The whole surface is the picture: the common case. */
static void check_runs( const char *what, int px, int py, int sw, int sh )
{
    check_picture( what, px, py, sw, sh, 0, 0, sw, sh );
}

/* The panel's own size, at the panel's edge, in its corner, and off it. */
static void test_panel_size( void )
{
    struct pointer_overlay_run runs[POINTER_OVERLAY_MAX_RUNS];
    int count, i, pixels = 0;

    count = pointer_overlay_runs( 100, 50, PANEL_W, PANEL_H, 0, 0, PANEL_W, PANEL_H, runs );
    for (i = 0; i < count; i++)
    {
        /* The arrow's own pixels, one sprite row at a time: 19 rows, three of
         * them with a gap under the wing. */
        if (runs[i].y1 - runs[i].y0 != 1) fail( "a row at a time", runs[i].x0, runs[i].y0, i, 0 );
        pixels += (runs[i].x1 - runs[i].x0) * (runs[i].y1 - runs[i].y0);
    }
    if (count != 22) fail( "runs of the arrow", 0, 0, count, 0 );
    {
        /* The arrow's own pixels: every one that is not clear. */
        int opaque = 0, x, y;

        for (y = 0; y < POINTER_CURSOR_H; y++)
            for (x = 0; x < POINTER_CURSOR_W; x++)
                if (pointer_cursor_shape[y][x] != '.') opaque++;
        if (pixels != opaque) fail( "opaque pixels", 0, 0, pixels, opaque );
    }
    check_runs( "720p", 100, 50, PANEL_W, PANEL_H );
    check_runs( "720p corner", 0, 0, PANEL_W, PANEL_H );
    check_runs( "720p right", PANEL_W - 1, 200, PANEL_W, PANEL_H );
    check_runs( "720p bottom", 200, PANEL_H - 1, PANEL_W, PANEL_H );
    check_runs( "720p far corner", PANEL_W - 1, PANEL_H - 1, PANEL_W, PANEL_H );

    /* A tip the picture cannot show is not on it at all. */
    if (pointer_overlay_runs( PANEL_W + 40, 50, PANEL_W, PANEL_H, 0, 0, PANEL_W, PANEL_H, runs ))
        fail( "off the surface", PANEL_W + 40, 50, 0, 0 );
}

/* A picture of another size than the panel: the arrow scales with it. */
static void test_scaled_surfaces( void )
{
    check_runs( "1080p", 640, 360, 1920, 1080 );
    check_runs( "1080p corner", 0, 0, 1920, 1080 );
    check_runs( "1080p far corner", PANEL_W - 1, PANEL_H - 1, 1920, 1080 );
    check_runs( "640x480", 100, 50, 640, 480 );
    check_runs( "640x480 far corner", PANEL_W - 1, PANEL_H - 1, 640, 480 );
    check_runs( "800x600", 300, 200, 800, 600 );
    check_runs( "1920x1440", 900, 600, 1920, 1440 );
    check_runs( "960x540", 700, 500, 960, 540 );
    check_runs( "320x240", 500, 400, 320, 240 );
    check_runs( "2560x1440", 1200, 700, 2560, 1440 );
}

/* The widescreen hack: the display shows the central 4:3 of an OpenGL window's
 * buffer, stretched over the whole panel.  The arrow belongs on that
 * rectangle, at that rectangle's scale -- not on the whole buffer, which is
 * where a plain scale puts it, too far right and too large. */
static void test_cropped_windows( void )
{
    struct pointer_overlay_run runs[POINTER_OVERLAY_MAX_RUNS];
    int count, i, widest = 0;

    /* A 1280x720 buffer cropped to 960x720 at x=160: the panel's 1280 columns
     * are those 960, so the arrow is three quarters of its panel width, and the
     * panel's pixel 0 is the buffer's pixel 160. */
    count = pointer_overlay_runs( 100, 50, PANEL_W, PANEL_H, 160, 0, 960, 720, runs );
    if (count < 1) fail( "a cropped window's runs", 0, 0, count, 0 );
    if (runs[0].x0 != 160 + (int)((long long)100 * 960 / PANEL_W)) fail( "the picture's x", runs[0].x0, 0, 0, 0 );
    if (runs[0].y0 != 50) fail( "the picture's y", 0, runs[0].y0, 0, 0 );
    for (i = 0; i < count; i++)
    {
        if (runs[i].x0 < 160 || runs[i].x1 > 160 + 960 || runs[i].y0 < 0 || runs[i].y1 > 720)
            fail( "inside the picture", runs[i].x0, runs[i].y0, i, 0 );
        if (runs[i].src_x1 - runs[i].src_x0 > POINTER_CURSOR_W) fail( "a sprite row at most", 0, 0, i, 0 );
        if (runs[i].x1 - runs[i].x0 > widest) widest = runs[i].x1 - runs[i].x0;
    }
    /* Smaller on the picture than on the panel, and never larger than the
     * sprite row it carries. */
    if (widest > POINTER_CURSOR_W) fail( "not enlarged", 0, 0, widest, 0 );
    check_picture( "cropped 1280x720", 100, 50, 1280, 720, 160, 0, 960, 720 );
    check_picture( "cropped, the corner", 0, 0, 1280, 720, 160, 0, 960, 720 );
    check_picture( "cropped, the far corner", PANEL_W - 1, PANEL_H - 1, 1280, 720, 160, 0, 960, 720 );

    /* A 1920x1080 buffer cropped to 1440x1080 at x=240: the arrow is 12*1440/1280
     * = 13.5 buffer pixels wide on the central 4:3 and never outside it. */
    check_picture( "cropped 1920x1080", 640, 360, 1920, 1080, 240, 0, 1440, 1080 );
    check_picture( "cropped 1920x1080, the left edge", 0, 0, 1920, 1080, 240, 0, 1440, 1080 );
    check_picture( "cropped 1920x1080, the right edge", PANEL_W - 1, 360, 1920, 1080, 240, 0, 1440, 1080 );

    /* The same buffer, not cropped: the arrow on the whole of it, larger than
     * the panel's own pixels. */
    check_picture( "uncropped 1920x1080", 640, 360, 1920, 1080, 0, 0, 1920, 1080 );
}

int main( void )
{
    test_panel_size();
    test_scaled_surfaces();
    test_cropped_windows();
    if (failures) return 1;
    puts( "pointer overlay: the arrow's opaque runs cover the arrow and nothing else, at any picture size, "
          "on the whole surface or on the part of it the display shows" );
    return 0;
}
