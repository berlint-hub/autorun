/* Host test for the starts that did not come back (source/start_guard.h). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../source/start_guard.h"

static char directory[64];

/* The file the count lives in is the settings file next to it with its own
 * suffix, wherever the settings are: the program-settings on the card, or the
 * settings a program on the USB keeps beside itself. */
static void test_path(void)
{
    char path[200], out[200];

    assert( nx_start_guard_path( out, sizeof(out),
                                 "sdmc:/switch/wine/program-settings/0123456789abcdef.wine-nx.txt" ) );
    assert( !strcmp( out, "sdmc:/switch/wine/program-settings/0123456789abcdef.start.txt" ) );
    assert( nx_start_guard_path( out, sizeof(out), "sdmc:/game/fable.wine-nx.txt" ) );
    assert( !strcmp( out, "sdmc:/game/fable.start.txt" ) );
    assert( nx_start_guard_path( out, sizeof(out), "sdmc:/game/no-extension" ) );
    assert( !strcmp( out, "sdmc:/game/no-extension.start.txt" ) );
    assert( nx_start_guard_path( out, sizeof(out), "bare.txt" ) );
    assert( !strcmp( out, "bare.start.txt" ) );

    /* A path that does not fit is not written short: the caller is told. */
    assert( nx_start_guard_path( path, sizeof(path), "sdmc:/switch/wine/program-settings/x.wine-nx.txt" ) );
    assert( !nx_start_guard_path( out, 8, path ) );
    assert( !nx_start_guard_path( out, sizeof(out), "" ) );
    assert( !nx_start_guard_path( out, sizeof(out), NULL ) );
}

static void test_count(void)
{
    char path[200];
    unsigned int streak;
    FILE *file;

    assert( nx_start_guard_path( path, sizeof(path), "settings.wine-nx.txt" ) );
    chdir( directory );

    /* No file yet: nothing has failed here, and that is not an error. */
    assert( !nx_start_guard_read( path, &streak ) && streak == 0 );

    assert( nx_start_guard_write( path, 2 ) );
    assert( nx_start_guard_read( path, &streak ) && streak == 2 );

    /* A file half written when the card went is a count of nothing, not a
     * number made of whatever survived. */
    assert( (file = fopen( path, "w" )) );
    fputs( "1x\n", file );
    fclose( file );
    assert( nx_start_guard_read( path, &streak ) && streak == 1 );
    assert( (file = fopen( path, "w" )) );
    fputs( "\n", file );
    fclose( file );
    assert( !nx_start_guard_read( path, &streak ) && streak == 0 );
    assert( (file = fopen( path, "w" )) );
    fputs( "9999999999\n", file );
    fclose( file );
    assert( nx_start_guard_read( path, &streak ) && streak == NX_START_GUARD_MAX );

    /* Clearing it is idempotent: a start that came home, twice, is not two
     * things to say. */
    assert( nx_start_guard_write( path, 3 ) );
    assert( nx_start_guard_clear( path ) );
    assert( !nx_start_guard_read( path, &streak ) && streak == 0 );
    assert( nx_start_guard_clear( path ) );
}

static void test_verdict(void)
{
    unsigned int i;

    /* Two failures in a row is the program, not a fluke; the count stops
     * growing where it stops meaning anything. */
    assert( !nx_start_guard_safe( 0 ) );
    assert( !nx_start_guard_safe( 1 ) );
    assert( nx_start_guard_safe( 2 ) );
    assert( nx_start_guard_safe( NX_START_GUARD_MAX ) );
    assert( nx_start_guard_next( 0 ) == 1 );
    assert( nx_start_guard_next( 1 ) == 2 );
    for (i = 0; i <= NX_START_GUARD_MAX + 3; i++)
        assert( nx_start_guard_next( i ) <= NX_START_GUARD_MAX );

    /* What the program ended on: an unhandled exception, in the forms Wine
     * gives them, and the ends that are not a failure. */
    assert( nx_start_guard_crash( 0xC0000005 ) );   /* access violation */
    assert( nx_start_guard_crash( 0xC000001D ) );   /* illegal instruction */
    assert( nx_start_guard_crash( 0xC00000FD ) );   /* stack overflow */
    assert( nx_start_guard_crash( 0xE06D7363 ) );   /* unhandled C++ exception */
    assert( nx_start_guard_crash( 0xC000013A ) );   /* control-C, still an end by itself */
    assert( !nx_start_guard_crash( 0 ) );           /* it exited cleanly */
    assert( !nx_start_guard_crash( ~0u ) );         /* the player quit, or it never said */
    assert( !nx_start_guard_crash( 1 ) );           /* a program's own error exit */
    assert( !nx_start_guard_crash( 0x80000003 ) );  /* a breakpoint, not a fault */
}

int main(void)
{
    strcpy( directory, "/tmp/start-guard-XXXXXX" );
    assert( mkdtemp( directory ) );

    test_path();
    test_count();
    test_verdict();

    rmdir( directory );
    puts( "Starts that did not come back: the count next to the settings, a half-written "
          "file counting nothing, a clean end clearing it, and which ends are failures passed" );
    return 0;
}
