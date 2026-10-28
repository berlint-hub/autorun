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

/* What the picture shows at a surface pixel: the sprite pixel there, or -1
 * when that pixel is not the arrow's. */
static int sampled_sprite( int x, int y, int px, int py, int sw, int sh )
{
    int column = (int)(((long long)x * 2 + 1) * PANEL_W / ((long long)sw * 2)) - px;
    int row = (int)(((long long)y * 2 + 1) * PANEL_H / ((long long)sh * 2)) - py;

    if (column < 0 || column >= POINTER_CURSOR_W || row < 0 || row >= POINTER_CURSOR_H) return -1;
    if (pointer_cursor_shape[row][column] == '.') return -1;
    return row * POINTER_CURSOR_W + column;
}

/* Every pixel of the arrow in exactly one run, and every other pixel in none,
 * with the runs' sources saying what the copy puts there. */
static void check_runs( const char *what, int px, int py, int sw, int sh )
{
    struct pointer_overlay_run runs[POINTER_OVERLAY_MAX_RUNS];
    static signed char *covered;
    int count = pointer_overlay_runs( px, py, PANEL_W, PANEL_H, sw, sh, runs ), i, x, y;
    int arrow = 0;

    if (count < 1) fail( what, px, py, count, 0 );
    if (!covered) covered = malloc( 2560 * 1440 );
    if (!covered) return;
    memset( covered, 0, (size_t)sw * sh );
    for (i = 0; i < count; i++)
        for (y = runs[i].y0; y < runs[i].y1; y++)
            for (x = runs[i].x0; x < runs[i].x1; x++)
            {
                if (x < 0 || x >= sw || y < 0 || y >= sh || covered[y * sw + x]++)
                    fail( what, x, y, i, 1 );
            }

    for (y = 0; y < sh; y++)
        for (x = 0; x < sw; x++)
        {
            int sprite = sampled_sprite( x, y, px, py, sw, sh );

            if ((sprite >= 0) != (covered[y * sw + x] == 1)) fail( what, x, y, sprite, covered[y * sw + x] );
            if (sprite >= 0) arrow++;
        }
    if (!arrow) fail( what, px, py, 0, 2 );

    /* A run's source is the sprite pixels its destination shows, from its top
     * row and left column on. */
    for (i = 0; i < count; i++)
    {
        const struct pointer_overlay_run *run = &runs[i];
        int first = sampled_sprite( run->x0, run->y0, px, py, sw, sh );
        int last = sampled_sprite( run->x1 - 1, run->y1 - 1, px, py, sw, sh );

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

        for (x = 0; x < run->x1 - run->x0; x++)
        {
            uint32_t pixel = pointer_overlay_pixel( run, x );
            int sprite = sampled_sprite( run->x0 + x, run->y0, px, py, sw, sh );

            if (pixel != 0xffffffffu && pixel != 0xff000000u) fail( what, run->x0 + x, run->y0, (int)pixel, i );
            if (sw == PANEL_W && sh == PANEL_H && sprite >= 0 &&
                pixel != (pointer_cursor_shape[sprite / POINTER_CURSOR_W][sprite % POINTER_CURSOR_W] == 'W' ?
                          0xffffffffu : 0xff000000u))
                fail( what, run->x0 + x, run->y0, (int)pixel, i );
        }
    }
}

/* The panel's own size, at the panel's edge, in its corner, and off it. */
static void test_panel_size( void )
{
    struct pointer_overlay_run runs[POINTER_OVERLAY_MAX_RUNS];
    int count, i, pixels = 0;

    count = pointer_overlay_runs( 100, 50, PANEL_W, PANEL_H, PANEL_W, PANEL_H, runs );
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
    if (pointer_overlay_runs( PANEL_W + 40, 50, PANEL_W, PANEL_H, PANEL_W, PANEL_H, runs ))
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

int main( void )
{
    test_panel_size();
    test_scaled_surfaces();
    if (failures) return 1;
    puts( "pointer overlay: the arrow's opaque runs cover the arrow and nothing else, at any picture size" );
    return 0;
}
