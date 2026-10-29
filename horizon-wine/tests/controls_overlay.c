/* Host test of the controls overlay (source/controls_overlay.c): what its
 * sections, tabs, rows, grid and capture do to a program's controls, and its
 * picture at two screen sizes (written as PNGs to the folder given, if any).
 *   clang -std=gnu11 -I source $(sdl2-config --cflags) \
 *       tests/controls_overlay.c source/controls_overlay.c source/input_profile.c \
 *       $(sdl2-config --libs) -lSDL2_ttf -lpng -lm
 */
#include <assert.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "controls_overlay.h"
#include "osk.h"

static const char *font_path;
static void *font_data;

int wine_nx_osk_font( const void **data, size_t *size )
{
    FILE *file = fopen( font_path, "rb" );
    long length;

    if (!file) return 0;
    fseek( file, 0, SEEK_END );
    length = ftell( file );
    fseek( file, 0, SEEK_SET );
    free( font_data );
    font_data = malloc( length );
    if (fread( font_data, 1, length, file ) != (size_t)length) length = 0;
    fclose( file );
    *data = font_data;
    *size = length;
    return length > 0;
}

/* The floating keyboard is not linked here: the overlay only hides it when it
 * opens, which the runtime does beside it. */
void wine_nx_osk_show( int show, unsigned int held )
{
    (void)show; (void)held;
}

/* The runtime's profile store is not linked either: record what the overlay
 * hands over, which is how remapping reaches a running game. */
static struct input_profile published;
void wine_nx_input_profile_publish( const struct input_profile *profile )
{
    published = *profile;
}

#define MS 1000000ull
static uint64_t now = 1000 * MS;

/* One reading, then 16 ms later. */
static void step( unsigned int held )
{
    wine_nx_controls_overlay_input( held, 0, 0, 0, 0, 0, 0, 0, now );
    now += 16 * MS;
}

static void press( unsigned int button )
{
    step( button );
    step( 0 );
}

/* A finger down, then up, on the console's 1280x720 panel. */
static int tap( int x, int y )
{
    int swallowed = wine_nx_controls_overlay_input( 0, 0, 0, 0, 0, 1, x, y, now );

    now += 16 * MS;
    wine_nx_controls_overlay_input( 0, 0, 0, 0, 0, 0, 0, 0, now );
    now += 16 * MS;
    return swallowed;
}

static int file_has( const char *root, const char *name, const char *text )
{
    char path[768], buffer[8192];
    FILE *file;
    size_t got;

    snprintf( path, sizeof(path), "%s/%s", root, name );
    file = fopen( path, "rb" );
    if (!file) return 0;
    got = fread( buffer, 1, sizeof(buffer) - 1, file );
    fclose( file );
    buffer[got] = 0;
    return strstr( buffer, text ) != NULL;
}

static void save( const char *dir, const char *name, int width, int height )
{
    struct wine_nx_osk_frame frame;
    png_image image = {0};
    char path[512];
    void *pixels;

    assert( wine_nx_controls_overlay_frame( width, height, &frame ) );
    pixels = malloc( (size_t)frame.width * frame.height * 4 );
    assert( wine_nx_controls_overlay_copy( width, height, pixels, frame.width * 4, 0 ) == frame.generation );
    if (dir)
    {
        snprintf( path, sizeof(path), "%s/%s", dir, name );
        image.version = PNG_IMAGE_VERSION;
        image.width = frame.width;
        image.height = frame.height;
        image.format = PNG_FORMAT_BGRA;
        assert( png_image_write_to_file( &image, path, 0, pixels, 0, NULL ) );
    }
    free( pixels );
}

int main( int argc, char **argv )
{
    const char *dir = argc > 2 ? argv[2] : NULL;
    char root[] = "/tmp/autorun-overlay-XXXXXX", game[768], settings[768];
    struct wine_nx_osk_frame frame, again;
    FILE *file;
    int i;

    font_path = argc > 1 ? argv[1] : "/System/Library/Fonts/Supplemental/Arial.ttf";
    assert( mkdtemp( root ) );
    snprintf( game, sizeof(game), "%s/septerra.exe", root );
    snprintf( settings, sizeof(settings), "%s/septerra.wine-nx.txt", root );

    /* Hidden, it reads nothing and draws nothing. */
    assert( !wine_nx_controls_overlay_visible() );
    assert( !wine_nx_controls_overlay_input( OSK_A, 0, 0, 0, 0, 0, 0, 0, now ) );
    assert( !wine_nx_controls_overlay_frame( 1280, 720, &frame ) );

    /* It opens over Septerra Core with its recommended layout, on the section
     * it was opened for: a panel down the right side of the screen, a margin
     * off every edge, as tall as the screen allows. */
    assert( wine_nx_controls_overlay_show( 1, 0, root, game ) );
    assert( wine_nx_controls_overlay_visible() );
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_MAPPING );
    assert( wine_nx_controls_overlay_selected() == 0 );
    assert( wine_nx_controls_overlay_frame( 1280, 720, &frame ) );
    assert( frame.width == 560 && frame.height == 684 && frame.x == 702 && frame.y == 18 );
    assert( frame.generation & 0x80000000u );  /* the keyboard's never are */
    assert( wine_nx_controls_overlay_frame( 1280, 720, &again ) && again.generation == frame.generation );
    {
        void *pixels = malloc( (size_t)frame.width * frame.height * 4 );

        assert( wine_nx_controls_overlay_copy( 1280, 720, pixels, frame.width * 4, 0 ) == frame.generation );
        free( pixels );
    }
    save( dir, "controls-mapping.png", 1280, 720 );

    /* The same panel on a bigger screen: everything about it is a share of
     * the screen, so a 1920x1080 one is the 1280x720 one, exactly bigger. */
    assert( wine_nx_controls_overlay_frame( 1920, 1080, &frame ) );
    assert( frame.width == 840 && frame.height == 1026 && frame.x == 1053 && frame.y == 27 );
    assert( wine_nx_controls_overlay_frame( 1280, 720, &again ) );
    save( dir, "controls-mapping-1080.png", 1920, 1080 );
    assert( wine_nx_controls_overlay_frame( 1280, 720, &frame ) );

    /* The sections are the tabs -- Input, Mapping, Mouse -- and Right and
     * Left walk them, round both ends. */
    press( OSK_RIGHT );
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_MOUSE );
    save( dir, "controls-mouse.png", 1280, 720 );
    press( OSK_RIGHT );
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_INPUT );
    save( dir, "controls-input.png", 1280, 720 );
    press( OSK_RIGHT );
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_MAPPING );
    press( OSK_LEFT );
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_INPUT );
    press( OSK_LEFT );
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_MOUSE );
    press( OSK_LEFT );
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_MAPPING );

    /* The mapping's rows: the d-pad and the left stick walk them, ZL and ZR
     * page them. */
    press( OSK_DOWN );
    press( OSK_DOWN );
    press( OSK_DOWN );
    assert( wine_nx_controls_overlay_selected() == 3 );
    press( OSK_ZR );
    assert( wine_nx_controls_overlay_selected() == 14 );
    press( OSK_ZL );
    assert( wine_nx_controls_overlay_selected() == 3 );
    wine_nx_controls_overlay_input( 0, 0, -30000, 0, 0, 0, 0, 0, now );
    now += 16 * MS;
    step( 0 );
    assert( wine_nx_controls_overlay_selected() == 4 );
    press( OSK_UP );
    assert( wine_nx_controls_overlay_selected() == 3 );
    press( OSK_ZL );                /* a page back: the first row, no wrap */
    assert( wine_nx_controls_overlay_selected() == 0 );

    /* A opens the row; the grid adds and takes away chord pieces (A began as
     * the game's left click and leaves as a right click), and B takes the chord
     * back to the program's controls at once. */
    press( OSK_A );
    save( dir, "controls-pick.png", 1280, 720 );
    press( OSK_A );                 /* Left click: out */
    press( OSK_RIGHT );
    press( OSK_A );                 /* Right click: in */
    press( OSK_B );
    assert( file_has( root, "septerra.wine-nx.txt", "key.A=0x101" ) );

    /* X clears the row to nothing, Y clears the row to the game's layout. */
    press( OSK_X );
    assert( file_has( root, "septerra.wine-nx.txt", "key.A=none" ) );
    press( OSK_Y );
    assert( !file_has( root, "septerra.wine-nx.txt", "key.A" ) );
    assert( wine_nx_controls_overlay_visible() );

    /* The section after it is the mouse speed, and its own rows are the stops:
     * Septerra recommends 150% here, so one row up is 100%. A sets it at once
     * -- into the running game as well -- and Y gives the game's own back. */
    press( OSK_RIGHT );
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_MOUSE );
    press( OSK_UP );
    press( OSK_A );
    assert( file_has( root, "septerra.wine-nx.txt", "mouse-speed=100" ) );
    assert( published.mouse_speed == 100 );
    save( dir, "controls-mouse.png", 1280, 720 );
    press( OSK_Y );                 /* the game's own again */
    assert( !file_has( root, "septerra.wine-nx.txt", "mouse-speed" ) );
    assert( published.mouse_speed == 150 );

    /* The first section is what the controller is. Septerra is a keyboard and
     * mouse game, so its run is in that mode already: A on the controller row
     * makes it the gamepad, written with everything else here. */
    press( OSK_LEFT );
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_MAPPING );
    press( OSK_LEFT );
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_INPUT );
    press( OSK_UP );
    press( OSK_A );
    assert( file_has( root, "septerra.wine-nx.txt", "input-mode=1" ) );
    assert( published.mode == INPUT_CONTROLLER );
    save( dir, "controls-input.png", 1280, 720 );
    /* The mapping follows it: what the pad sends is captured, not picked from
     * a grid of keys. */
    press( OSK_RIGHT );
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_MAPPING );
    press( OSK_A );
    save( dir, "controls-capture.png", 1280, 720 );
    press( OSK_B );                 /* out of the capture */
    assert( wine_nx_controls_overlay_visible() );
    /* And back: the keyboard and mouse row, and the grid again, where A takes
     * its own left click away. */
    press( OSK_LEFT );
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_INPUT );
    press( OSK_DOWN );
    press( OSK_A );
    assert( file_has( root, "septerra.wine-nx.txt", "input-mode=2" ) );
    assert( published.mode == INPUT_KEYBOARD_MOUSE );
    press( OSK_RIGHT );
    press( OSK_A );
    press( OSK_A );
    press( OSK_B );
    assert( file_has( root, "septerra.wine-nx.txt", "key.A=none" ) );
    press( OSK_Y );
    assert( !file_has( root, "septerra.wine-nx.txt", "key.A" ) );

    /* A finger works the same panel: a tap on a tab is that tab, a tap on a
     * row opens it, and a tap off the panel is the program's. */
    assert( tap( 801, 77 ) );                       /* the Input tab */
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_INPUT );
    press( OSK_RIGHT );
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_MAPPING );
    assert( tap( 800, 183 ) );                      /* the second row */
    assert( wine_nx_controls_overlay_selected() == 1 );
    press( OSK_B );                                 /* out of the grid */
    assert( wine_nx_controls_overlay_visible() );
    assert( !tap( 100, 100 ) );                     /* off the panel */
    assert( wine_nx_controls_overlay_selected() == 1 );
    /* On a 1920x1080 screen the presenters draw for, a finger still lands on
     * the second row: its pixels are the panel's, scaled into that screen. */
    assert( wine_nx_controls_overlay_frame( 1920, 1080, &frame ) );
    assert( tap( 800, 182 ) );
    assert( wine_nx_controls_overlay_selected() == 1 );
    press( OSK_B );
    assert( wine_nx_controls_overlay_frame( 1280, 720, &frame ) );
    /* The hints at the bottom are Back. */
    assert( tap( 800, 670 ) );
    assert( !wine_nx_controls_overlay_visible() );

    /* B closes it; it opens again on the same file, and buttons held while it
     * opens are the opening itself, not a press. */
    assert( wine_nx_controls_overlay_show( 1, 0, root, game ) );
    assert( wine_nx_controls_overlay_frame( 1280, 720, &frame ) );
    press( OSK_MINUS );
    assert( !wine_nx_controls_overlay_visible() );
    assert( wine_nx_controls_overlay_show( 1, OSK_DOWN | OSK_A, root, game ) );
    step( OSK_DOWN | OSK_A );
    step( 0 );
    assert( wine_nx_controls_overlay_selected() == 0 );
    press( OSK_DOWN );
    assert( wine_nx_controls_overlay_selected() == 1 );
    press( OSK_MINUS );
    assert( !wine_nx_controls_overlay_visible() );

    /* In controller mode the row captures what is pressed: ZL, six rows down,
     * becomes R when R is pressed. */
    file = fopen( settings, "wb" );
    assert( file );
    fputs( "input-mode=1\n", file );
    fclose( file );
    assert( wine_nx_controls_overlay_show( 1, 0, root, game ) );
    assert( wine_nx_controls_overlay_tab() == WINE_NX_CONTROLS_TAB_MAPPING );
    for (i = 0; i < 6; i++) press( OSK_DOWN );
    assert( wine_nx_controls_overlay_selected() == 6 );
    press( OSK_A );
    save( dir, "controls-capture.png", 1280, 720 );
    press( OSK_R );
    assert( wine_nx_controls_overlay_selected() == 6 );
    assert( file_has( root, "septerra.wine-nx.txt", "pad.ZL=0x6" ) );
    press( OSK_MINUS );
    assert( !wine_nx_controls_overlay_visible() );

    wine_nx_controls_overlay_reset();
    assert( !unlink( settings ) );
    assert( !rmdir( root ) );
    free( font_data );
    puts( "Controls overlay: sections, tabs, rows, grid chords, mode, speed, capture, taps and picture passed" );
    return 0;
}
