/* Host test for horizon-wine/source/screen_fit.h: stretching a picture onto
 * the panel the way the Switch display compositor scales a layer's buffer.
 * Built by .github/workflows/build.yml ("Host tests"). */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "screen_fit.h"

static int failures;

static void expect( const char *what, double x, double y, double want_x, double want_y )
{
    if (fabs( x - want_x ) < 1e-9 && fabs( y - want_y ) < 1e-9) return;
    fprintf( stderr, "FAIL %s: got %g,%g want %g,%g\n", what, x, y, want_x, want_y );
    failures++;
}

int main(void)
{
    {
        struct { int w, h, cw, ch; } cases[] =
        {
            { 1920, 1080, 1440, 1080 },  /* a widescreen window: crop the sides */
            { 1280, 720, 960, 720 },
            { 640, 480, 640, 480 },      /* 4:3 stays as it is */
            { 800, 600, 800, 600 },
            { 1024, 768, 1024, 768 },
            { 1000, 1000, 1000, 750 },   /* a square picture: crop top and bottom */
            { 641, 480, 640, 480 },
        };
        size_t i;

        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        {
            int w = cases[i].w, h = cases[i].h;

            screen_crop_4_3( &w, &h );
            if (w != cases[i].cw || h != cases[i].ch)
            {
                fprintf( stderr, "FAIL crop 4:3 %dx%d -> %dx%d, want %dx%d\n",
                         cases[i].w, cases[i].h, w, h, cases[i].cw, cases[i].ch );
                failures++;
            }
        }
    }

    double sx, sy;

    /* The widescreen-patched game: 1920x1080 onto the 1280x720 panel is an
     * exact 16:9 fit, uniform. */
    screen_fill_scales( 1920, 1080, 1280, 720, &sx, &sy );
    expect( "1080p on 720p", sx, sy, 1280.0 / 1920, 720.0 / 1080 );

    /* The panel's own size: identity. */
    screen_fill_scales( 1280, 720, 1280, 720, &sx, &sy );
    expect( "720p on 720p", sx, sy, 1.0, 1.0 );

    /* A 4:3 game window fills the panel too -- stretched, the widescreen
     * look the Switch's own scaler gives every layer. */
    screen_fill_scales( 1440, 1080, 1280, 720, &sx, &sy );
    expect( "4:3 fills 16:9", sx, sy, 1280.0 / 1440, 720.0 / 1080 );

    screen_fill_scales( 640, 480, 1280, 720, &sx, &sy );
    expect( "640x480 fills", sx, sy, 2.0, 1.5 );

    /* Degenerate sizes do not divide by zero. */
    screen_fill_scales( 0, 0, 1280, 720, &sx, &sy );
    if (!(sx > 0) || !(sy > 0))
    {
        fprintf( stderr, "FAIL degenerate: got %g,%g\n", sx, sy );
        failures++;
    }

    /* Null outputs are allowed. */
    screen_fill_scales( 1920, 1080, 1280, 720, NULL, NULL );

    /* The Scaling setting: where the picture lands on the 1280x720 panel. */
    {
        struct { int mode, src_w, src_h, x, y, w, h; int line; } cases[] =
        {
            { SCREEN_SCALING_FULL,     640, 480,    0,   0, 1280, 720, __LINE__ }, /* stretched */
            { SCREEN_SCALING_FULL,    1920, 1080,   0,   0, 1280, 720, __LINE__ },
            { SCREEN_SCALING_ASPECT,   640, 480,  160,   0,  960, 720, __LINE__ }, /* 4:3, bars at the sides */
            { SCREEN_SCALING_ASPECT,   800, 600,  160,   0,  960, 720, __LINE__ },
            { SCREEN_SCALING_ASPECT,  1024, 768,  160,   0,  960, 720, __LINE__ },
            { SCREEN_SCALING_ASPECT,  1920, 1080,   0,   0, 1280, 720, __LINE__ }, /* 16:9: no bars */
            { SCREEN_SCALING_ASPECT,  1000, 1000, 280,   0,  720, 720, __LINE__ }, /* square: bars left and right */
            { SCREEN_SCALING_ASPECT,  1920, 800,    0,  93, 1280, 533, __LINE__ }, /* wider than 16:9: bars above and below */
            { SCREEN_SCALING_INTEGER,  640, 480,  320, 120,  640, 480, __LINE__ }, /* one whole step fits */
            { SCREEN_SCALING_INTEGER,  640, 360,    0,   0, 1280, 720, __LINE__ }, /* the panel's half: 2 steps */
            { SCREEN_SCALING_INTEGER,  320, 180,    0,   0, 1280, 720, __LINE__ }, /* 4 steps */
            { SCREEN_SCALING_INTEGER,  800, 600,  240,  60,  800, 600, __LINE__ },
            { SCREEN_SCALING_INTEGER, 1920, 1080,   0,   0, 1280, 720, __LINE__ }, /* larger: scaled down, aspect kept */
            { SCREEN_SCALING_INTEGER, 1000, 1000, 280,   0,  720, 720, __LINE__ }, /* taller than the panel: aspect kept */
            { SCREEN_SCALING_STRETCH_43, 640, 480, 0,   0, 1280, 720, __LINE__ }, /* the caller cropped: stretched */
            { SCREEN_SCALING_STRETCH_43, 960, 720, 0,   0, 1280, 720, __LINE__ },
        };
        size_t i;

        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        {
            int x, y, w, h;

            screen_fit_rect( cases[i].mode, cases[i].src_w, cases[i].src_h, 1280, 720, &x, &y, &w, &h );
            if (x == cases[i].x && y == cases[i].y && w == cases[i].w && h == cases[i].h) continue;
            fprintf( stderr, "FAIL rect mode %d %dx%d: got %d,%d %dx%d want %d,%d %dx%d\n",
                     cases[i].mode, cases[i].src_w, cases[i].src_h, x, y, w, h,
                     cases[i].x, cases[i].y, cases[i].w, cases[i].h );
            failures++;
        }
    }

    /* Whatever the mode and the size, the picture stays inside the panel and
     * is centred in it: a bar is never wider on one side than on the other. */
    {
        static const int modes[] = { SCREEN_SCALING_FULL, SCREEN_SCALING_ASPECT, SCREEN_SCALING_INTEGER };
        int w, h, m;

        for (m = 0; m < 3; m++)
        for (w = 1; w <= 2048; w += 7)
        for (h = 1; h <= 2048; h += 11)
        {
            int x, y, rw, rh;

            screen_fit_rect( modes[m], w, h, 1280, 720, &x, &y, &rw, &rh );
            /* a leftover pixel of an odd difference goes to the far side */
            if (x < 0 || y < 0 || rw < 1 || rh < 1 || x + rw > 1280 || y + rh > 720 ||
                abs( x - (1280 - (x + rw)) ) > 1 || abs( y - (720 - (y + rh)) ) > 1)
            {
                fprintf( stderr, "FAIL rect bounds mode %d %dx%d: %d,%d %dx%d\n",
                         modes[m], w, h, x, y, rw, rh );
                failures++;
                m = 3;
                break;
            }
        }
    }

    /* Whole steps are whole: no bars of a fraction of a pixel, and the picture
     * is an exact multiple of the source both ways. */
    {
        int w, h;

        for (w = 1; w <= 640; w += 13)
        for (h = 1; h <= 360; h += 7)
        {
            int x, y, rw, rh;

            screen_fit_rect( SCREEN_SCALING_INTEGER, w, h, 1280, 720, &x, &y, &rw, &rh );
            if (rw % w || rh % h || rw / w != rh / h)
            {
                fprintf( stderr, "FAIL integer steps %dx%d: %dx%d\n", w, h, rw, rh );
                failures++;
                w = 641;
                break;
            }
        }
    }

    /* The compute blit's workgroup grid: it covers the picture's rectangle and
     * never starts or ends outside the image, because an invocation that lands
     * outside would write outside it. */
    {
        unsigned int bx, by, gx, gy;

        /* 4:3 in the middle of the panel: whole groups on both sides */
        if (!screen_dispatch_grid( 160, 0, 960, 720, 1280, 720, &bx, &by, &gx, &gy ) ||
            bx != 20 || by != 0 || gx != 120 || gy != 90)
        {
            fprintf( stderr, "FAIL grid 160,0 960x720: %u,%u %ux%u\n", bx, by, gx, gy );
            failures++;
        }
        /* the whole image: no base to speak of */
        if (!screen_dispatch_grid( 0, 0, 1280, 720, 1280, 720, &bx, &by, &gx, &gy ) ||
            bx != 0 || by != 0 || gx != 160 || gy != 90)
        {
            fprintf( stderr, "FAIL grid full: %u,%u %ux%u\n", bx, by, gx, gy );
            failures++;
        }
        /* an odd rectangle: the grid is a group wider than the picture, which
         * is what the extra invocations painting the bars black are */
        if (!screen_dispatch_grid( 93, 11, 533, 333, 1280, 720, &bx, &by, &gx, &gy ) ||
            bx != 11 || by != 1 || gx != 68 || gy != 42)
        {
            fprintf( stderr, "FAIL grid 93,11 533x333: %u,%u %ux%u\n", bx, by, gx, gy );
            failures++;
        }
        /* an image smaller than a group cannot be dispatched at all */
        if (screen_dispatch_grid( 0, 0, 4, 4, 4, 4, &bx, &by, &gx, &gy ))
        {
            fprintf( stderr, "FAIL grid tiny image: covered\n" );
            failures++;
        }
    }

    /* Every rectangle of every image: the grid stays inside the image, starts
     * no later than the rectangle and ends no earlier, and is never empty. */
    {
        static const int images[][2] = { { 1280, 720 }, { 1920, 1080 }, { 640, 480 }, { 1013, 707 } };
        int i, w, h;

        for (i = 0; i < 4; i++)
        for (w = 1; w < images[i][0]; w += 37)
        for (h = 1; h < images[i][1]; h += 29)
        {
            int x = (images[i][0] - w) / 2, y = (images[i][1] - h) / 2;
            unsigned int bx = 0, by = 0, gx = 0, gy = 0;

            if (!screen_dispatch_grid( x, y, w, h, images[i][0], images[i][1], &bx, &by, &gx, &gy ) ||
                !gx || !gy ||
                (int)(bx * 8) > x || (int)(by * 8) > y ||
                (int)((bx + gx) * 8) < x + w || (int)((by + gy) * 8) < y + h ||
                (int)((bx + gx) * 8) > (images[i][0] / 8) * 8 ||
                (int)((by + gy) * 8) > (images[i][1] / 8) * 8)
            {
                fprintf( stderr, "FAIL grid %dx%d at %d,%d in %dx%d: %u,%u %ux%u\n",
                         w, h, x, y, images[i][0], images[i][1], bx, by, gx, gy );
                failures++;
                i = 4;
                break;
            }
        }
    }

    if (failures) return 1;
    printf( "screen_fit: all cases pass\n" );
    return 0;
}
