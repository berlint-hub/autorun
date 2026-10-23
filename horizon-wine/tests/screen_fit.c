/* Host test for horizon-wine/source/screen_fit.h: fitting a picture onto the
 * panel, centred.  Built by .github/workflows/build.yml ("Host tests"). */
#include <stdio.h>
#include <stdlib.h>

#include "screen_fit.h"

static int failures;

static void expect( const char *what, int x, int y, int w, int h,
                    int want_x, int want_y, int want_w, int want_h )
{
    if (x == want_x && y == want_y && w == want_w && h == want_h) return;
    fprintf( stderr, "FAIL %s: got %d,%d %dx%d want %d,%d %dx%d\n",
             what, x, y, w, h, want_x, want_y, want_w, want_h );
    failures++;
}

int main(void)
{
    int x, y, w, h;

    /* The widescreen-patched game: 1920x1080 on the 1280x720 panel fills it
     * exactly (both 16:9), which is the case the user reported. */
    screen_fit_rect( 1920, 1080, 1280, 720, &x, &y, &w, &h );
    expect( "1080p fills 720p", x, y, w, h, 0, 0, 1280, 720 );

    /* The same picture is the panel's own size: identity. */
    screen_fit_rect( 1280, 720, 1280, 720, &x, &y, &w, &h );
    expect( "720p on 720p", x, y, w, h, 0, 0, 1280, 720 );

    /* A 4:3 game scales up to fit the height, centred between the bars. */
    screen_fit_rect( 640, 480, 1280, 720, &x, &y, &w, &h );
    expect( "640x480 on 720p", x, y, w, h, 160, 0, 960, 720 );

    screen_fit_rect( 800, 600, 1280, 720, &x, &y, &w, &h );
    expect( "800x600 on 720p", x, y, w, h, 160, 0, 960, 720 );

    /* A taller-than-wide picture gets bars left and right. */
    screen_fit_rect( 1024, 768, 1280, 720, &x, &y, &w, &h );
    expect( "1024x768 on 720p", x, y, w, h, 160, 0, 960, 720 );

    /* Ultra-wide: bars above and below, both the same. */
    screen_fit_rect( 3000, 1000, 1280, 720, &x, &y, &w, &h );
    expect( "ultrawide on 720p", x, y, w, h, 0, 146, 1280, 427 );

    /* A small picture is scaled up (a handheld wants the screen used). */
    screen_fit_rect( 320, 240, 1280, 720, &x, &y, &w, &h );
    expect( "320x240 on 720p", x, y, w, h, 160, 0, 960, 720 );

    /* Degenerate sizes do not divide by zero. */
    screen_fit_rect( 0, 0, 1280, 720, &x, &y, &w, &h );
    if (w < 1 || h < 1 || w > 1280 || h > 720)
    {
        fprintf( stderr, "FAIL degenerate: got %d,%d %dx%d\n", x, y, w, h );
        failures++;
    }

    /* Null outputs are allowed. */
    screen_fit_rect( 1920, 1080, 1280, 720, NULL, NULL, NULL, NULL );

    if (failures) return 1;
    printf( "screen_fit: all cases pass\n" );
    return 0;
}
