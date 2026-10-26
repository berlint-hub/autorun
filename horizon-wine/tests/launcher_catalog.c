#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../source/launcher_catalog.h"

/* A PE header as small as the launcher's reader needs, to say what a program
 * asks of the address space. relocations is the size of the base relocation
 * directory, stripped and dynamic the two header flags that answer the same
 * question in their own way. */
static void write_pe( const char *path, int plus, unsigned long long image_base,
                      unsigned int relocations, int stripped, int dynamic )
{
    unsigned char image[0x400] = {0};
    unsigned char *pe = image + 0x80, *optional = pe + 24;
    unsigned char *directories = optional + (plus ? 112 : 96);
    int base_offset = plus ? 24 : 28, base_bytes = plus ? 8 : 4;
    FILE *file;

    image[0] = 'M'; image[1] = 'Z';
    image[0x3c] = 0x80;
    pe[0] = 'P'; pe[1] = 'E';
    pe[4] = plus ? 0x64 : 0x4c; pe[5] = plus ? 0x86 : 0x01;   /* Machine: x86-64 or i386 */
    pe[4 + 16] = plus ? 240 : 224;                       /* SizeOfOptionalHeader */
    pe[4 + 18] = stripped ? 0x01 : 0x00;                 /* IMAGE_FILE_RELOCS_STRIPPED */
    optional[0] = 0x0b; optional[1] = plus ? 0x02 : 0x01;   /* PE32+ or PE32 */
    for (int i = 0; i < base_bytes; i++) optional[base_offset + i] = (image_base >> (8 * i)) & 0xff;
    optional[70] = dynamic ? 0x40 : 0x00;                /* IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE */
    optional[plus ? 108 : 92] = 16;                      /* NumberOfRvaAndSizes */
    for (int i = 0; i < 4; i++) directories[5 * 8 + 4 + i] = (relocations >> (8 * i)) & 0xff;

    file = fopen( path, "wb" );
    assert( file );
    assert( fwrite( image, 1, sizeof(image), file ) == sizeof(image) );
    assert( !fclose( file ) );
}

static void check_address_space(void)
{
    char path[] = "/tmp/wine-nx-program-XXXXXX";
    int fd = mkstemp( path );
    FILE *file;

    assert( fd >= 0 );
    close( fd );

    /* Relocations and a low image base: it goes wherever there is room. */
    write_pe( path, 0, 0x400000, 0x1000, 0, 0 );
    assert( launcher_program_address_space( path ) == LAUNCHER_ADDRESS_ANY );

    /* None, and linked for an address only the low 4 GB has. */
    write_pe( path, 0, 0x400000, 0, 0, 0 );
    assert( launcher_program_address_space( path ) == LAUNCHER_ADDRESS_LOW );
    write_pe( path, 0, 0x400000, 0x1000, 1, 0 );
    assert( launcher_program_address_space( path ) == LAUNCHER_ADDRESS_LOW );

    /* Asking to be moved answers the question by itself. */
    write_pe( path, 0, 0x400000, 0, 0, 1 );
    assert( launcher_program_address_space( path ) == LAUNCHER_ADDRESS_ANY );

    /* A 64-bit program linked above 4 GB, where a 32-bit address space reaches
     * nothing: only PE32+ can say such a base at all. */
    write_pe( path, 1, 0x140000000ull, 0, 0, 0 );
    assert( launcher_program_address_space( path ) == LAUNCHER_ADDRESS_ANY );
    write_pe( path, 1, 0x400000, 0, 0, 0 );
    assert( launcher_program_address_space( path ) == LAUNCHER_ADDRESS_LOW );

    /* Not a program at all. */
    file = fopen( path, "wb" );
    assert( file );
    assert( fprintf( file, "this is not a program" ) > 0 );
    assert( !fclose( file ) );
    assert( launcher_program_address_space( path ) == LAUNCHER_ADDRESS_UNKNOWN );
    unlink( path );
    assert( launcher_program_address_space( path ) == LAUNCHER_ADDRESS_UNKNOWN );
}

/* A run is timed by started_at and credited the next time the launcher is
 * opened; nothing a broken clock says becomes play time. */
static void check_play_time(void)
{
    struct launcher_catalog catalog;
    struct launcher_catalog_entry *entry;
    char text[64];

    launcher_catalog_init( &catalog );
    assert( launcher_catalog_add( &catalog, "sdmc:/games/one.exe", "One" ) == 0 );
    assert( launcher_catalog_add( &catalog, "sdmc:/games/two.exe", "Two" ) == 1 );
    entry = &catalog.entries[0];

    /* A two-minute run, and the other entry was not played. */
    entry->started_at = 1000;
    entry->last_played = 1000;
    assert( launcher_catalog_settle_sessions( &catalog, 1120 ) == 1 );
    assert( entry->play_seconds == 120 && entry->started_at == 0 && entry->last_played == 1000 );
    assert( catalog.entries[1].play_seconds == 0 && catalog.entries[1].started_at == 0 );
    /* Settling again does not count the same run twice. */
    assert( launcher_catalog_settle_sessions( &catalog, 9000 ) == 0 && entry->play_seconds == 120 );

    /* Two runs add up. */
    entry->started_at = 9000;
    assert( launcher_catalog_settle_sessions( &catalog, 9060 ) == 1 );
    assert( entry->play_seconds == 180 && entry->last_played == 1000 );

    /* A clock that went backwards closes the run without counting it. */
    entry->started_at = 5000;
    assert( launcher_catalog_settle_sessions( &catalog, 4000 ) == 1 );
    assert( entry->play_seconds == 180 && entry->started_at == 0 );

    /* A console left on for days is credited with one day, not with all of it. */
    entry->started_at = 10;
    assert( launcher_catalog_settle_sessions( &catalog, 10 + LAUNCHER_SESSION_MAX + 5000 ) == 1 );
    assert( entry->play_seconds == 180 + LAUNCHER_SESSION_MAX );

    /* And the count saturates instead of wrapping. */
    entry->play_seconds = UINT_MAX - 5;
    entry->started_at = 100;
    assert( launcher_catalog_settle_sessions( &catalog, 1000 ) == 1 );
    assert( entry->play_seconds == UINT_MAX );

    /* A run whose start was never written is not a run. */
    entry->started_at = 0;
    assert( launcher_catalog_settle_sessions( &catalog, 1000 ) == 0 );

    launcher_playtime_text( text, sizeof(text), 0 );
    assert( !strcmp( text, "Never" ) );
    launcher_playtime_text( text, sizeof(text), 30 );
    assert( !strcmp( text, "<1m" ) );
    launcher_playtime_text( text, sizeof(text), 60 );
    assert( !strcmp( text, "1m" ) );
    launcher_playtime_text( text, sizeof(text), 3599 );
    assert( !strcmp( text, "59m" ) );
    launcher_playtime_text( text, sizeof(text), 3600 );
    assert( !strcmp( text, "1h" ) );
    launcher_playtime_text( text, sizeof(text), 3725 );
    assert( !strcmp( text, "1h 2m" ) );
    launcher_playtime_text( text, sizeof(text), 7200 );
    assert( !strcmp( text, "2h" ) );
    launcher_playtime_text( text, sizeof(text), 360000 );
    assert( !strcmp( text, "100h" ) );
    /* A buffer of no room is written to nothing, not overrun. */
    launcher_playtime_text( text, 0, 3725 );
    launcher_lastplayed_text( text, sizeof(text), 0, 5000 );
    assert( !strcmp( text, "Never" ) );

    launcher_lastplayed_text( text, sizeof(text), 5000, 5000 );
    assert( !strcmp( text, "Today" ) );
    launcher_lastplayed_text( text, sizeof(text), 5000, 4000 );
    assert( !strcmp( text, "Today" ) );
    launcher_lastplayed_text( text, sizeof(text), 1000, 1000 + 86400 );
    assert( !strcmp( text, "Yesterday" ) );
    launcher_lastplayed_text( text, sizeof(text), 1000, 1000 + 3 * 86400 );
    assert( !strcmp( text, "3 days ago" ) );
    launcher_lastplayed_text( text, sizeof(text), 1000, 1000 + 20 * 86400 );
    assert( !strcmp( text, "2 weeks ago" ) );
    launcher_lastplayed_text( text, sizeof(text), 1000, 1000 + 45 * 86400 );
    assert( !strcmp( text, "6 weeks ago" ) );
    launcher_lastplayed_text( text, sizeof(text), 1000, 1000 + 100 * 86400 );
    assert( !strcmp( text, "3 months ago" ) );
    launcher_lastplayed_text( text, sizeof(text), 1000, 1000 + 400 * 86400 );
    assert( !strcmp( text, "1 year ago" ) );
    launcher_lastplayed_text( text, sizeof(text), 1000, 1000 + 800 * 86400 );
    assert( !strcmp( text, "2 years ago" ) );
}

/* A card written before play time was kept: read as it is, with no time. */
static void check_older_catalog(void)
{
    char path[] = "/tmp/wine-nx-catalog-old-XXXXXX";
    struct launcher_catalog catalog;
    FILE *file;
    int fd = mkstemp( path ), version = 0;

    assert( fd >= 0 );
    close( fd );
    file = fopen( path, "w" );
    assert( file );
    assert( fprintf( file, "version=3\nnext-id=9\n\n[game 1]\npath=sdmc:/games/old.exe\n"
                           "title=Old\nadded-order=1\nlaunched-order=2\nfavorite=1\n" ) > 0 );
    assert( !fclose( file ) );
    assert( launcher_catalog_load( &catalog, path ) == LAUNCHER_CATALOG_OK );
    assert( catalog.count == 1 && !strcmp( catalog.entries[0].title, "Old" ) );
    assert( catalog.entries[0].favorite == 1 && catalog.entries[0].launched_order == 2 );
    assert( !catalog.entries[0].play_seconds && !catalog.entries[0].last_played );
    assert( !catalog.entries[0].started_at );
    /* Saving it brings it up to this build's version. */
    assert( launcher_catalog_save( &catalog, path ) );
    file = fopen( path, "r" );
    assert( file && fscanf( file, "version=%d", &version ) == 1 );
    assert( !fclose( file ) );
    assert( version == LAUNCHER_CATALOG_VERSION );
    assert( launcher_catalog_load( &catalog, path ) == LAUNCHER_CATALOG_OK && catalog.count == 1 );
    unlink( path );
}

int main(void)
{
    struct launcher_catalog written, read;
    char path[] = "/tmp/wine-nx-catalog-XXXXXX";
    char legacy[] = "/tmp/wine-nx-legacy-XXXXXX";
    FILE *file;
    int fd = mkstemp( path ), index;
    assert( fd >= 0 );
    close( fd );
    unlink( path );

    launcher_catalog_init( &written );
    index = launcher_catalog_add( &written, "sdmc:/Games/A=B/[One]/game.exe", "A %= Game" );
    assert( index == 0 );
    written.entries[index].favorite = 1;
    written.entries[index].launched_order = 8;
    written.entries[index].play_seconds = 3725;
    written.entries[index].last_played = 1700000000u;
    snprintf( written.entries[index].square_art, sizeof(written.entries[index].square_art), "/art/square.png" );
    snprintf( written.entries[index].portrait_art, sizeof(written.entries[index].portrait_art), "/art/portrait.png" );
    snprintf( written.entries[index].hero_art, sizeof(written.entries[index].hero_art), "/art/hero.png" );
    assert( launcher_catalog_add( &written, "SDMC:/games/a=b/[one]/GAME.EXE", "duplicate" ) == 0 );
    assert( launcher_catalog_save( &written, path ) );
    assert( launcher_catalog_load( &read, path ) == LAUNCHER_CATALOG_OK );
    assert( read.count == 1 );
    assert( !strcmp( read.entries[0].path, "sdmc:/Games/A=B/[One]/game.exe" ) );
    assert( !strcmp( read.entries[0].title, "A %= Game" ) );
    assert( read.entries[0].favorite == 1 && read.entries[0].launched_order == 8 );
    assert( read.entries[0].play_seconds == 3725 && read.entries[0].last_played == 1700000000u );
    assert( read.entries[0].started_at == 0 );
    assert( !strcmp( read.entries[0].square_art, "/art/square.png" ) );
    assert( !strcmp( read.entries[0].portrait_art, "/art/portrait.png" ) );
    assert( !strcmp( read.entries[0].hero_art, "/art/hero.png" ) );

    launcher_catalog_remove( &read, 0 );
    assert( read.count == 0 );
    assert( launcher_catalog_save( &read, path ) );
    assert( launcher_catalog_load( &written, path ) == LAUNCHER_CATALOG_OK && written.count == 0 );
    assert( written.next_id == 2 );
    assert( launcher_catalog_add( &written, "ums0:/Another/Game.exe", "Another" ) == 0 );
    assert( written.entries[0].id == 2 );

    fd = mkstemp( legacy );
    assert( fd >= 0 );
    close( fd );
    file = fopen( legacy, "w" );
    assert( file );
    assert( fprintf( file, "sdmc:/games/one.exe\nsdmc:/games/ONE.exe\nsdmc:/games/missing.exe\n" ) > 0 );
    assert( !fclose( file ) );
    launcher_catalog_init( &written );
    assert( launcher_catalog_import_legacy( &written, legacy ) );
    assert( written.count == 2 );
    assert( !strcmp( written.entries[1].path, "sdmc:/games/missing.exe" ) );
    unlink( path );
    unlink( legacy );
    check_address_space();
    check_play_time();
    check_older_catalog();
    puts( "launcher catalog: entries, art, ordering, play time, legacy import and address space passed" );
    return 0;
}
