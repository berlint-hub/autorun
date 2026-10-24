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

    if (failures) return 1;
    printf( "screen_fit: all cases pass\n" );
    return 0;
}
