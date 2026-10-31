/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
/*
 * The starts of a program that did not come back.
 *
 * A game that dies the same way every time takes its owner round the same loop:
 * start it, watch it crash, wonder which of the settings it was. The runtime
 * therefore counts the starts in a row that ended badly, next to the program's
 * settings, and the start after two of them begins with the Compatibility fast
 * paths (runtime.c). One clean exit puts the count back to nothing, so a game
 * that crashed once months ago is not treated as broken.
 *
 * What counts as "did not come back" is the program's own NT status: an
 * unhandled exception leaves its code as the exit code (0xC0000005, 0xE06D7363)
 * and is one; an exit code 0 is not, and neither is a quit the player asked for,
 * which reports no code at all.
 *
 * Small enough for a header, so tests/start_guard.c can drive it on the host.
 */
#ifndef WINE_NX_START_GUARD_H
#define WINE_NX_START_GUARD_H

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The start after this many failures in a row runs with the safe paths: the
 * first crash may be a fluke, the second one is the program. */
#define NX_START_GUARD_SAFE_AT 2u
/* A count worth keeping in a byte, and not worth growing past: a program that
 * has failed this often fails every time. */
#define NX_START_GUARD_MAX 9u

/* The file that holds the count, next to the settings it belongs to: the same
 * name with its own suffix, so a card that keeps the settings on the USB keeps
 * this there too, and deleting the settings deletes it with them. */
static inline int nx_start_guard_path( char *out, size_t size, const char *settings_path )
{
    const char *slash, *dot;
    size_t stem;
    int length;

    if (!settings_path || !settings_path[0]) return 0;
    slash = strrchr( settings_path, '/' );
    dot = strchr( slash ? slash + 1 : settings_path, '.' );
    stem = dot ? (size_t)(dot - settings_path) : strlen( settings_path );
    length = snprintf( out, size, "%.*s.start.txt", (int)stem, settings_path );
    return length > 0 && (size_t)length < size;
}

/* The count the card holds. A file that is not there, or does not hold a
 * number, is a count of nothing: the program has not failed here before.
 * Returns 0 when there was nothing to read, which a caller does not have to
 * tell apart from a count of 0. */
static inline int nx_start_guard_read( const char *path, unsigned int *streak )
{
    char text[16];
    unsigned int value = 0;
    size_t i;
    FILE *file;

    *streak = 0;
    if (!path || !(file = fopen( path, "r" ))) return 0;
    if (!fgets( text, sizeof(text), file ))
    {
        fclose( file );
        return 0;
    }
    fclose( file );
    if (text[0] < '0' || text[0] > '9') return 0;
    for (i = 0; text[i] >= '0' && text[i] <= '9' && i + 1 < sizeof(text); i++)
        if (value < NX_START_GUARD_MAX) value = value * 10 + (unsigned int)(text[i] - '0');
    *streak = value;
    return 1;
}

static inline int nx_start_guard_write( const char *path, unsigned int streak )
{
    FILE *file;
    int ok;

    if (!path || !(file = fopen( path, "w" ))) return 0;
    ok = fprintf( file, "%u\n", streak ) > 0;
    if (fclose( file )) ok = 0;
    return ok;
}

/* A start that came home: there is nothing to count any more. A file that is
 * already gone is gone. */
static inline int nx_start_guard_clear( const char *path )
{
    if (!path || !path[0]) return 0;
    return unlink( path ) == 0 || errno == ENOENT;
}

/* Whether the start about to begin should be a safe one. */
static inline int nx_start_guard_safe( unsigned int streak )
{
    return streak >= NX_START_GUARD_SAFE_AT;
}

/* The count this start leaves behind if it does not come back. */
static inline unsigned int nx_start_guard_next( unsigned int streak )
{
    return streak + 1 > NX_START_GUARD_MAX ? NX_START_GUARD_MAX : streak + 1;
}

/* Whether the program's end was its own failure. Wine gives NtTerminateProcess
 * the exception's status for one it did not handle, so the two high bits mark
 * it; ~0 is a start that never ended by itself, and the chord's quit and a
 * crash the console swallowed alike, which is not the program's verdict. */
static inline int nx_start_guard_crash( unsigned int exit_code )
{
    return exit_code != ~0u && (exit_code & 0xC0000000u) == 0xC0000000u;
}

#endif /* WINE_NX_START_GUARD_H */
