#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "launcher_catalog.h"

static void trim( char *text )
{
    char *start = text, *end;
    while (*start == ' ' || *start == '\t') start++;
    if (start != text) memmove( text, start, strlen( start ) + 1 );
    end = text + strlen( text );
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) --end;
    *end = 0;
}

static int hex_value( unsigned char c )
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int copy_value( char *out, size_t size, const char *value )
{
    size_t i = 0;
    while (*value)
    {
        unsigned char c = *value++;
        if (c == '%')
        {
            /* Both digits write_value puts after a % are here or the line is not
             * one it wrote: reading a single digit and stepping two bytes over
             * it would walk past the value's end and decode the line before. */
            int high = hex_value( (unsigned char)value[0] ), low;
            if (high < 0 || (low = hex_value( (unsigned char)value[1] )) < 0) return 0;
            c = (unsigned char)(high * 16 + low);
            value += 2;
        }
        if (!c || c == '\r' || c == '\n' || i + 1 >= size) return 0;
        out[i++] = c;
    }
    out[i] = 0;
    return 1;
}

static int write_value( FILE *file, const char *key, const char *value )
{
    const unsigned char *p = (const unsigned char *)value;
    if (fprintf( file, "%s=", key ) < 0) return 0;
    while (*p)
    {
        if (*p == '%' || *p == '\r' || *p == '\n' || *p == '=' || *p == '[' || *p == ']')
        {
            if (fprintf( file, "%%%02X", *p++ ) < 0) return 0;
        }
        else if (fputc( *p++, file ) == EOF) return 0;
    }
    return fputc( '\n', file ) != EOF;
}

void launcher_catalog_init( struct launcher_catalog *catalog )
{
    memset( catalog, 0, sizeof(*catalog) );
    catalog->next_id = catalog->next_order = 1;
}

/* Little-endian fields of a PE header, read by hand: the launcher has no Windows
 * headers, and only four of them are wanted. */
static unsigned int pe_u16( const unsigned char *p ) { return p[0] | (p[1] << 8); }
static unsigned long long pe_u32( const unsigned char *p )
{
    return (unsigned long long)p[0] | ((unsigned long long)p[1] << 8) |
           ((unsigned long long)p[2] << 16) | ((unsigned long long)p[3] << 24);
}

enum launcher_address_space launcher_program_address_space( const char *path )
{
    /* Enough for the DOS stub's one useful field and the whole PE header. */
    unsigned char head[0x400];
    const unsigned char *pe, *optional, *directories;
    unsigned long long lfanew, image_base;
    unsigned int magic, characteristics, dll_characteristics, directory_count, relocations;
    size_t read;
    FILE *file = fopen( path, "rb" );

    if (!file) return LAUNCHER_ADDRESS_UNKNOWN;
    read = fread( head, 1, sizeof(head), file );
    fclose( file );
    if (read < 0x40 || head[0] != 'M' || head[1] != 'Z') return LAUNCHER_ADDRESS_UNKNOWN;
    lfanew = pe_u32( head + 0x3c );
    /* The COFF header is 20 bytes and the smallest optional header 96. */
    if (lfanew + 4 + 20 + 96 > read) return LAUNCHER_ADDRESS_UNKNOWN;
    pe = head + lfanew;
    if (pe[0] != 'P' || pe[1] != 'E' || pe[2] || pe[3]) return LAUNCHER_ADDRESS_UNKNOWN;
    characteristics = pe_u16( pe + 4 + 18 );
    optional = pe + 4 + 20;
    magic = pe_u16( optional );
    if (magic == 0x10b)        /* PE32 */
    {
        image_base = pe_u32( optional + 28 );
        directory_count = (unsigned int)pe_u32( optional + 92 );
        directories = optional + 96;
    }
    else if (magic == 0x20b)   /* PE32+ */
    {
        image_base = pe_u32( optional + 24 ) | (pe_u32( optional + 28 ) << 32);
        directory_count = (unsigned int)pe_u32( optional + 108 );
        directories = optional + 112;
    }
    else return LAUNCHER_ADDRESS_UNKNOWN;
    dll_characteristics = pe_u16( optional + 70 );
    /* Directory 5 is the base relocations. */
    if (directories + 6 * 8 > head + read) return LAUNCHER_ADDRESS_UNKNOWN;
    relocations = directory_count > 5 ? (unsigned int)pe_u32( directories + 5 * 8 + 4 ) : 0;

    /* It can be moved if it says where to, or if it allows being moved at all.
     * IMAGE_FILE_RELOCS_STRIPPED is 0x0001, IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE
     * 0x0040. An image above 4 GB is out of a 32-bit address space's reach and
     * cannot want one. */
    if (relocations && !(characteristics & 0x0001)) return LAUNCHER_ADDRESS_ANY;
    if (dll_characteristics & 0x0040) return LAUNCHER_ADDRESS_ANY;
    if (image_base >= 0x100000000ull) return LAUNCHER_ADDRESS_ANY;
    return LAUNCHER_ADDRESS_LOW;
}

/* An order, or 0 for anything a hand-edited line cannot mean. 0 is what the
 * loader gives a line without one, and it must not reach the counter: an order
 * of UINT_MAX would make next_order wrap to 0 and leave every new game with no
 * place in the list at all. */
static unsigned int parse_order( const char *value )
{
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul( value, &end, 10 );
    if (errno || *end || parsed >= UINT_MAX) return 0;
    return (unsigned int)parsed;
}

/* A count of seconds, or 0 for anything a hand-edited line cannot mean. */
static unsigned int parse_seconds( const char *value )
{
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul( value, &end, 10 );
    if (errno || *end || parsed > UINT_MAX) return 0;
    return (unsigned int)parsed;
}

int launcher_catalog_find( const struct launcher_catalog *catalog, const char *path )
{
    int i;
    for (i = 0; i < catalog->count; i++)
        if (!strcasecmp( catalog->entries[i].path, path )) return i;
    return -1;
}

int launcher_catalog_add( struct launcher_catalog *catalog, const char *path, const char *title )
{
    struct launcher_catalog_entry *entry;
    int found = launcher_catalog_find( catalog, path );
    if (found >= 0) return found;
    if (!path[0] || strpbrk( path, "\r\n" ) || strlen( path ) >= sizeof(catalog->entries[0].path) ||
        catalog->count >= LAUNCHER_MAX_ENTRIES) return -1;
    entry = &catalog->entries[catalog->count];
    memset( entry, 0, sizeof(*entry) );
    entry->id = catalog->next_id++;
    entry->added_order = catalog->next_order++;
    snprintf( entry->path, sizeof(entry->path), "%s", path );
    snprintf( entry->title, sizeof(entry->title), "%s", title ? title : "" );
    return catalog->count++;
}

void launcher_catalog_remove( struct launcher_catalog *catalog, int index )
{
    if (index < 0 || index >= catalog->count) return;
    memmove( &catalog->entries[index], &catalog->entries[index + 1],
             (catalog->count - index - 1) * sizeof(catalog->entries[0]) );
    catalog->count--;
}

enum launcher_catalog_result launcher_catalog_load( struct launcher_catalog *catalog, const char *path )
{
    char line[1200];
    struct launcher_catalog parsed;
    struct launcher_catalog_entry *entry = NULL;
    FILE *file = fopen( path, "r" );
    int version = 0, invalid = 0;

    if (!file) return errno == ENOENT ? LAUNCHER_CATALOG_MISSING : LAUNCHER_CATALOG_IO_ERROR;
    launcher_catalog_init( &parsed );
    while (fgets( line, sizeof(line), file ))
    {
        char *equals, *key, *value;
        unsigned int n;
        if (!strchr( line, '\n' ) && !feof( file )) { invalid = 1; break; }
        trim( line );
        if (!line[0] || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[')
        {
            char tail;
            if (sscanf( line, "[game %u]%c", &n, &tail ) != 1 || !n || parsed.count >= LAUNCHER_MAX_ENTRIES)
            { invalid = 1; break; }
            entry = &parsed.entries[parsed.count++];
            memset( entry, 0, sizeof(*entry) );
            entry->id = n;
            if (n >= parsed.next_id) parsed.next_id = n + 1;
            continue;
        }
        if (!(equals = strchr( line, '=' ))) { invalid = 1; break; }
        *equals = 0; key = line; value = equals + 1; trim( key ); trim( value );
        if (!strcmp( key, "version" ) && !entry) version = atoi( value );
        else if (!strcmp( key, "next-id" ) && !entry)
        {
            char *end;
            unsigned long id;
            errno = 0;
            id = strtoul( value, &end, 10 );
            if (errno || *end || !id || id >= UINT_MAX) { invalid = 1; break; }
            parsed.next_id = id;
        }
        else if (!entry) { invalid = 1; break; }
        else if (!strcmp( key, "path" )) invalid |= !copy_value( entry->path, sizeof(entry->path), value );
        else if (!strcmp( key, "title" )) invalid |= !copy_value( entry->title, sizeof(entry->title), value );
        else if (!strcmp( key, "square-art" )) invalid |= !copy_value( entry->square_art, sizeof(entry->square_art), value );
        else if (!strcmp( key, "portrait-art" ) || !strcmp( key, "landscape-art" ))
            invalid |= !copy_value( entry->portrait_art, sizeof(entry->portrait_art), value );
        else if (!strcmp( key, "hero-art" )) invalid |= !copy_value( entry->hero_art, sizeof(entry->hero_art), value );
        else if (!strcmp( key, "favorite" )) entry->favorite = atoi( value ) == 1;
        else if (!strcmp( key, "added-order" )) entry->added_order = parse_order( value );
        else if (!strcmp( key, "launched-order" )) entry->launched_order = parse_order( value );
        else if (!strcmp( key, "play-seconds" )) entry->play_seconds = parse_seconds( value );
        else if (!strcmp( key, "last-played" )) entry->last_played = parse_seconds( value );
        else if (!strcmp( key, "started-at" )) entry->started_at = parse_seconds( value );
    }
    if (ferror( file )) invalid = 1;
    fclose( file );
    if (version < 2 || version > LAUNCHER_CATALOG_VERSION) invalid = 1;
    for (int i = 0; !invalid && i < parsed.count; i++)
    {
        if (!parsed.entries[i].path[0]) invalid = 1;
        if (!parsed.entries[i].added_order) parsed.entries[i].added_order = parsed.next_order++;
        if (parsed.entries[i].added_order >= parsed.next_order) parsed.next_order = parsed.entries[i].added_order + 1;
        if (parsed.entries[i].launched_order >= parsed.next_order) parsed.next_order = parsed.entries[i].launched_order + 1;
        for (int j = 0; j < i; j++)
            if (parsed.entries[j].id == parsed.entries[i].id ||
                !strcasecmp( parsed.entries[j].path, parsed.entries[i].path )) invalid = 1;
    }
    if (invalid) return LAUNCHER_CATALOG_INVALID;
    *catalog = parsed;
    return LAUNCHER_CATALOG_OK;
}

int launcher_catalog_save( const struct launcher_catalog *catalog, const char *path )
{
    char temp[600], backup[600];
    FILE *file;
    int i, ok = 1;
    if ((size_t)snprintf( temp, sizeof(temp), "%s.tmp", path ) >= sizeof(temp) ||
        (size_t)snprintf( backup, sizeof(backup), "%s.bak", path ) >= sizeof(backup)) return 0;
    if (!(file = fopen( temp, "w" ))) return 0;
    ok &= fprintf( file, "version=%d\nnext-id=%u\n", LAUNCHER_CATALOG_VERSION, catalog->next_id ) > 0;
    for (i = 0; ok && i < catalog->count; i++)
    {
        const struct launcher_catalog_entry *entry = &catalog->entries[i];
        ok &= fprintf( file, "\n[game %u]\n", entry->id ) > 0;
        ok &= write_value( file, "path", entry->path );
        ok &= write_value( file, "title", entry->title );
        ok &= fprintf( file, "added-order=%u\nlaunched-order=%u\nfavorite=%d\n",
                       entry->added_order, entry->launched_order, entry->favorite ) > 0;
        if (entry->play_seconds) ok &= fprintf( file, "play-seconds=%u\n", entry->play_seconds ) > 0;
        if (entry->last_played) ok &= fprintf( file, "last-played=%u\n", entry->last_played ) > 0;
        if (entry->started_at) ok &= fprintf( file, "started-at=%u\n", entry->started_at ) > 0;
        if (entry->square_art[0]) ok &= write_value( file, "square-art", entry->square_art );
        if (entry->portrait_art[0]) ok &= write_value( file, "portrait-art", entry->portrait_art );
        if (entry->hero_art[0]) ok &= write_value( file, "hero-art", entry->hero_art );
    }
    if (fflush( file )) ok = 0;
    if (fclose( file )) ok = 0;
    if (!ok) { remove( temp ); return 0; }
    remove( backup );
    rename( path, backup );
    if (rename( temp, path ))
    {
        rename( backup, path );
        remove( temp );
        return 0;
    }
    return 1;
}

int launcher_catalog_import_legacy( struct launcher_catalog *catalog, const char *path )
{
    char line[600];
    FILE *file = fopen( path, "r" );
    if (!file) return errno == ENOENT;
    while (fgets( line, sizeof(line), file ))
    {
        line[strcspn( line, "\r\n" )] = 0;
        trim( line );
        if (line[0] && launcher_catalog_add( catalog, line, "" ) < 0) { fclose( file ); return 0; }
    }
    /* Read before closing: a failure to close would hide one to read. */
    {
        int read = ferror( file );

        if (fclose( file )) return 0;
        return !read;
    }
}

/* How long a run is credited: from the time it began to the time it ended,
 * never more than a day, and nothing at all for a clock that went backwards or
 * was never set -- which says nothing about how long anything ran. */
static unsigned int run_seconds( unsigned int started, unsigned int ended )
{
    unsigned int elapsed;

    if (!started || ended <= started) return 0;
    elapsed = ended - started;
    return elapsed > LAUNCHER_SESSION_MAX ? LAUNCHER_SESSION_MAX : elapsed;
}

int launcher_catalog_credit_run( struct launcher_catalog *catalog, const char *path,
                                 unsigned int started, unsigned int ended )
{
    struct launcher_catalog_entry *entry;
    unsigned int elapsed = run_seconds( started, ended );
    int index;

    if (!started || (index = launcher_catalog_find( catalog, path )) < 0) return 0;
    entry = &catalog->entries[index];
    if (elapsed > UINT_MAX - entry->play_seconds) entry->play_seconds = UINT_MAX;
    else entry->play_seconds += elapsed;
    if (!entry->last_played) entry->last_played = started;
    entry->started_at = 0;
    return 1;
}

int launcher_catalog_settle_sessions( struct launcher_catalog *catalog, unsigned int now )
{
    int i, changed = 0;

    for (i = 0; i < catalog->count; i++)
    {
        struct launcher_catalog_entry *entry = &catalog->entries[i];
        unsigned int elapsed;

        if (!entry->started_at) continue;
        elapsed = run_seconds( entry->started_at, now );
        if (elapsed > UINT_MAX - entry->play_seconds) entry->play_seconds = UINT_MAX;
        else entry->play_seconds += elapsed;
        if (!entry->last_played) entry->last_played = entry->started_at;
        entry->started_at = 0;
        changed++;
    }
    return changed;
}

int launcher_session_write( const char *file, const char *path, unsigned int started, unsigned int alive )
{
    char temp[600];
    FILE *out;
    int ok;

    if (!file[0] || !path[0] || !started) return 0;
    if ((size_t)snprintf( temp, sizeof(temp), "%s.tmp", file ) >= sizeof(temp)) return 0;
    if (!(out = fopen( temp, "w" ))) return 0;
    ok = write_value( out, "path", path );
    ok &= fprintf( out, "started=%u\nalive=%u\n", started, alive > started ? alive : started ) > 0;
    if (fflush( out )) ok = 0;
    if (fclose( out )) ok = 0;
    if (!ok) { remove( temp ); return 0; }
    if (rename( temp, file ))
    {
        /* A card's rename does not always replace: the file goes first. */
        remove( file );
        if (rename( temp, file )) { remove( temp ); return 0; }
    }
    return 1;
}

int launcher_session_read( const char *file, struct launcher_session *session )
{
    char text[1600], *line, *next;
    FILE *in = fopen( file, "r" );
    size_t got;
    int have_path = 0, read_error;

    memset( session, 0, sizeof(*session) );
    if (!in) return 0;
    got = fread( text, 1, sizeof(text) - 1, in );
    read_error = ferror( in );
    fclose( in );
    text[got] = 0;
    /* Every line this file writes ends with a newline, and a file that stops
     * anywhere else was still being written when the card went: the start it
     * half wrote is a wrong start, which would be credited as a whole day, and
     * not a short run. */
    if (read_error || !got || text[got - 1] != '\n' || got == sizeof(text) - 1) return 0;
    for (line = text; *line; line = next)
    {
        char *equals, *key, *value;

        if ((next = strchr( line, '\n' ))) *next++ = 0;
        trim( line );
        if (!line[0] || line[0] == '#' || line[0] == ';') continue;
        if (!(equals = strchr( line, '=' ))) return 0;
        *equals = 0; key = line; value = equals + 1; trim( key ); trim( value );
        if (!strcmp( key, "path" ))
        {
            if (have_path || !copy_value( session->path, sizeof(session->path), value )) return 0;
            have_path = 1;
        }
        else if (!strcmp( key, "started" )) session->started = parse_seconds( value );
        else if (!strcmp( key, "alive" )) session->alive = parse_seconds( value );
        else return 0;   /* not a line this file writes */
    }
    if (!have_path || !session->started) return 0;
    if (!session->alive) session->alive = session->started;
    return 1;
}

int launcher_session_clear( const char *file )
{
    if (!remove( file ) || errno == ENOENT) return 1;
    return 0;
}

int launcher_catalog_settle_session( struct launcher_catalog *catalog, const char *file, unsigned int now )
{
    struct launcher_session session;
    int changed;

    if (!launcher_session_read( file, &session ))
    {
        /* Nothing this wrote: a card that lost power mid-write, or a program
         * that never began. Either way it is not a run, and it is not left to
         * be read again. */
        if (file[0]) launcher_session_clear( file );
        return 0;
    }
    /* The last time the runtime said it was up is the end of the run: the
     * hours a console slept in a game are not hours of play. */
    if (now && now < session.alive) session.alive = now;
    changed = launcher_catalog_credit_run( catalog, session.path, session.started, session.alive );
    launcher_session_clear( file );
    return changed;
}

void launcher_playtime_text( char *out, size_t size, unsigned int seconds )
{
    unsigned int minutes = seconds / 60, hours = minutes / 60;

    if (!seconds) snprintf( out, size, "Never" );
    else if (!minutes) snprintf( out, size, "<1m" );
    else if (!hours) snprintf( out, size, "%um", minutes );
    else if (minutes % 60 == 0) snprintf( out, size, "%uh", hours );
    else snprintf( out, size, "%uh %um", hours, minutes % 60 );
}

void launcher_library_summary( char *out, size_t size, int games, unsigned int play_seconds )
{
    char played[32];

    if (games <= 0) snprintf( out, size, "No games yet" );
    else if (!play_seconds) snprintf( out, size, "%d %s, not played yet", games, games == 1 ? "game" : "games" );
    else
    {
        launcher_playtime_text( played, sizeof(played), play_seconds );
        snprintf( out, size, "%d %s, %s played", games, games == 1 ? "game" : "games", played );
    }
}

void launcher_lastplayed_text( char *out, size_t size, unsigned int last_played, unsigned int now )
{
    unsigned int days, count;

    if (!last_played)
    {
        snprintf( out, size, "Never" );
        return;
    }
    /* A clock that went backwards, or one the console never set, cannot say
     * the game was started before today. */
    if (now <= last_played)
    {
        snprintf( out, size, "Today" );
        return;
    }
    days = (now - last_played) / 86400;
    if (!days)
    {
        snprintf( out, size, "Today" );
        return;
    }
    if (days == 1)
    {
        snprintf( out, size, "Yesterday" );
        return;
    }
    if (days < 14)
    {
        snprintf( out, size, "%u days ago", days );
        return;
    }
    if (days < 60)
    {
        count = days / 7;
        snprintf( out, size, count == 1 ? "1 week ago" : "%u weeks ago", count );
        return;
    }
    if (days < 365)
    {
        count = days / 30;
        snprintf( out, size, count == 1 ? "1 month ago" : "%u months ago", count );
        return;
    }
    count = days / 365;
    snprintf( out, size, count == 1 ? "1 year ago" : "%u years ago", count );
}
