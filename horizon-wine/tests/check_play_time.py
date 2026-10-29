#!/usr/bin/env python3
"""The play time a game keeps, as the runtime keeps it.

The library's counter is written by the runtime -- the one that starts the
program and is still there as it ends -- and read by the launcher. This takes
the runtime's own play-time block out of src/runtime.c and runs it against the
real catalog code on a temporary card, because the part that goes wrong is not
the arithmetic (launcher_catalog.c's own test has that) but who is credited,
when the run in progress is cleared, and what a run that was never closed leaves
behind.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
runtime = (root / "horizon-wine/source/runtime.c").read_text()
catalog = root / "horizon-wine/source/launcher_catalog.c"

start = runtime.index(" * Play time")
start = runtime.rindex("/*", 0, start)
end = runtime.index("static void *log_flusher( void *arg )")
block = runtime[start:end]
assert "library_run_begin" in block and "library_run_end" in block, "the play-time block moved"

fixture = r'''
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "launcher_catalog.h"

static void log_line( const char *format, ... )
{
    va_list args;

    va_start( args, format );
    vprintf( format, args );
    va_end( args );
    putchar( '\n' );
}

''' + block + r'''

static struct launcher_catalog *read_library(void)
{
    static struct launcher_catalog catalog;

    assert( launcher_catalog_load( &catalog, RUNTIME_DIR "/" LAUNCHER_CATALOG_FILE ) == LAUNCHER_CATALOG_OK );
    assert( catalog.count == 1 );
    return &catalog;
}

int main( void )
{
    const char *game = "sdmc:/Roms/Windows/Doom 3/doom3.exe";
    const char *session = RUNTIME_DIR "/" LAUNCHER_SESSION_FILE;
    struct launcher_session in_progress;
    struct launcher_catalog catalog;
    char text[64];

    remove( RUNTIME_DIR "/" LAUNCHER_CATALOG_FILE );
    remove( session );
    launcher_catalog_init( &catalog );
    assert( launcher_catalog_add( &catalog, game, "Doom 3" ) == 0 );
    assert( launcher_catalog_save( &catalog, RUNTIME_DIR "/" LAUNCHER_CATALOG_FILE ) );

    /* The log says a run's length the way a person reads it. */
    run_time_text( text, sizeof(text), 0 );
    assert( !strcmp( text, "under a minute" ) );
    run_time_text( text, sizeof(text), 30 );
    assert( !strcmp( text, "<1m" ) );
    run_time_text( text, sizeof(text), 2470 );
    assert( !strcmp( text, "41m" ) );
    run_time_text( text, sizeof(text), 22380 );
    assert( !strcmp( text, "6h 13m" ) );
    playtime_text( text, sizeof(text), 0 );
    assert( !strcmp( text, "nothing played" ) );
    playtime_text( text, sizeof(text), 22380 );
    assert( !strcmp( text, "6h 13m played" ) );

    /* A program the library does not hold has no play time to keep, and the
     * runtime says nothing about one it was not asked to run. */
    assert( !library_run_begin( "sdmc:/elsewhere/thing.exe" ) );
    assert( !library_run_begin( "" ) );
    assert( access( session, F_OK ) != 0 );

    /* A run this runtime closes: written down as it begins, credited as it
     * ends, and nothing left for the launcher to count again. */
    assert( library_run_begin( game ) );
    assert( launcher_session_read( session, &in_progress ) );
    assert( !strcmp( in_progress.path, game ) );
    assert( in_progress.started && in_progress.alive == in_progress.started );
    library_run_keep();
    library_run_end();
    assert( access( session, F_OK ) != 0 );
    catalog = *read_library();
    assert( catalog.entries[0].last_played != 0 );
    assert( catalog.entries[0].play_seconds <= 2 );
    assert( !catalog.entries[0].started_at );

    /* A run that ends without this runtime closing it is left on the card with
     * the last time it was seen up, and the launcher credits that and no
     * more: the hours a console slept in a game are not hours of play. */
    assert( library_run_begin( game ) );
    assert( launcher_session_read( session, &in_progress ) );
    assert( in_progress.started );
    assert( launcher_catalog_settle_session( &catalog, session, in_progress.started + 3600 ) == 1 );
    assert( catalog.entries[0].play_seconds <= 4 );
    assert( access( session, F_OK ) != 0 );

    /* Two runs add up, and a second settle adds nothing. */
    assert( library_run_begin( game ) );
    library_run_end();
    assert( launcher_catalog_settle_session( &catalog, session, 0 ) == 0 );

    puts( "play time: a run the runtime closes, one it leaves, and the log lines that say so passed" );
    return 0;
}
'''


def main():
    compiler = os.environ.get("WINE_NX_HOST_CC", "cc")
    with tempfile.TemporaryDirectory() as build:
        source = Path(build) / "play_time.c"
        source.write_text(fixture)
        binary = Path(build) / "play_time"
        subprocess.run(
            [compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
             f'-DRUNTIME_DIR="{build}"', "-I", str(root / "horizon-wine/source"),
             str(source), str(catalog), "-o", str(binary)],
            check=True,
        )
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        assert result.returncode == 0, (result.stdout, result.stderr)
        lines = result.stdout.splitlines()
        for expected in ("[PLAY] doom3.exe began; nothing played so far",
                         "[PLAY] doom3.exe ran under a minute; nothing played so far"):
            assert expected in lines, (expected, lines)
        assert lines[-1].startswith("play time: "), lines[-1]
        assert Path(build, "launcher-library-v2.ini").exists()
        print(result.stdout, end="")


if __name__ == "__main__":
    main()
