/* Host test for the copy of the launcher's settings (source/launcher_backup.c):
 * what it takes, what the manifest says, what a restore puts back, and what it
 * refuses. */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../source/launcher_backup.h"

static int exists( const char *path )
{
    return !access( path, F_OK );
}

/* A tree of the card, removed the way the launcher removes one: deepest first,
 * with the folders after their files. */
static void remove_tree( const char *path )
{
    char child[512];
    struct stat st;
    struct dirent *entry;
    DIR *dir;

    if (lstat( path, &st )) return;
    if (!S_ISDIR( st.st_mode ))
    {
        remove( path );
        return;
    }
    if ((dir = opendir( path )))
    {
        while ((entry = readdir( dir )))
        {
            if (!strcmp( entry->d_name, "." ) || !strcmp( entry->d_name, ".." )) continue;
            snprintf( child, sizeof(child), "%s/%s", path, entry->d_name );
            remove_tree( child );
        }
        closedir( dir );
    }
    rmdir( path );
}

static void write_text( const char *path, const char *text )
{
    FILE *file = fopen( path, "w" );

    assert( file );
    assert( fputs( text, file ) >= 0 );
    assert( !fclose( file ) );
}

static void read_text( const char *path, char *out, size_t size )
{
    FILE *file = fopen( path, "r" );
    size_t got;

    assert( file );
    got = fread( out, 1, size - 1, file );
    out[got] = 0;
    assert( !fclose( file ) );
}

static void make_dirs( const char *path )
{
    char copy[512], *at;

    snprintf( copy, sizeof(copy), "%s", path );
    for (at = copy + 1; *at; at++)
        if (*at == '/')
        {
            *at = 0;
            mkdir( copy, 0777 );
            *at = '/';
        }
    mkdir( copy, 0777 );
}

static void check_stamp(void)
{
    char text[LAUNCHER_BACKUP_NAME];

    launcher_backup_stamp( text, sizeof(text), 0 );
    assert( !strcmp( text, "1970-01-01-0000" ) );
    /* 2026-10-26 17:45 UTC, the time a switch in Prague would show at 18:45. */
    launcher_backup_stamp( text, sizeof(text), 1793036700u );
    assert( !strcmp( text, "2026-10-26-1745" ) );
    /* A leap day, and the last second of a year. */
    launcher_backup_stamp( text, sizeof(text), 1709164800u );
    assert( !strcmp( text, "2024-02-29-0000" ) );
    launcher_backup_stamp( text, sizeof(text), 1735689599u );
    assert( !strcmp( text, "2024-12-31-2359" ) );
    launcher_backup_stamp( text, 6, 1793036700u );
    assert( !strncmp( text, "2026-", 5 ) );   /* a short buffer is never overflowed */

    launcher_backup_title( text, sizeof(text), "2026-10-26-1745" );
    assert( !strcmp( text, "2026-10-26 17:45" ) );
    launcher_backup_title( text, sizeof(text), "2026-10-26-1745-2" );
    assert( !strcmp( text, "2026-10-26 17:45" ) );
    launcher_backup_title( text, sizeof(text), "junk" );
    assert( !strcmp( text, "junk" ) );
}

static void check_detail(void)
{
    struct launcher_backup backup;
    char text[64];

    memset( &backup, 0, sizeof(backup) );
    backup.files = 0;
    launcher_backup_detail( text, sizeof(text), &backup );
    assert( !strcmp( text, "0 files, 0 B" ) );
    backup.files = 1;
    backup.bytes = 512;
    launcher_backup_detail( text, sizeof(text), &backup );
    assert( !strcmp( text, "1 file, 512 B" ) );
    backup.files = 12;
    backup.bytes = 1433;
    launcher_backup_detail( text, sizeof(text), &backup );
    assert( !strcmp( text, "12 files, 1.3 KB" ) );
    backup.files = 3;
    backup.bytes = 3 * 1024 * 1024 + 512 * 1024;
    launcher_backup_detail( text, sizeof(text), &backup );
    assert( !strcmp( text, "3 files, 3.5 MB" ) );
    launcher_backup_detail( text, 8, &backup );
    assert( !strcmp( text, "3 files" ) );
}

/* The card the tests write to: a runtime folder beside the games, with a game
 * on the SD card and one on USB. */
static void stage_card(void)
{
    make_dirs( "sdmc:/games" );
    make_dirs( "runtime/config" );
    make_dirs( "runtime/program-settings" );
    write_text( "runtime/launcher.txt", "show-hidden=0\nhide-missing=1\n" );
    write_text( "runtime/keys.txt", "jump=a\n" );
    write_text( "runtime/args.txt", "C:\\games\\SPEED2.EXE -windowed\n" );
    write_text( "runtime/config/keys.txt", "jump=b\n" );
    write_text( "runtime/config/settings.json", "{\n  \"verbose-log\": true\n}\n" );
    write_text( "runtime/program-settings/0000000000000001.wine-nx.txt", "cpu=fex\n" );
    write_text( "sdmc:/games/SPEED2.wine-nx.txt", "cpu=fex-opt\nframe-limit=30\n" );
    write_text( "sdmc:/games/SPEED2.args.txt", "-windowed\n" );
    write_text( "sdmc:/games/SPEED2.keys.txt", "jump=x\n" );
}

static void check_create_and_restore(void)
{
    static const char *const programs[] = { "sdmc:/games/SPEED2.EXE", "ums0:/games/USB.EXE", "sdmc:/games/EMPTY.EXE" };
    static const char *const no_programs[] = { NULL };
    struct launcher_backup made, kept[LAUNCHER_BACKUP_MAX];
    char error[192], text[1024];
    unsigned int restored;
    int count, i;

    stage_card();
    assert( launcher_backup_create( "runtime", "2026-10-26-1745", programs, 3, 0, &made, error, sizeof(error) ) );
    assert( !strcmp( made.name, "2026-10-26-1745" ) );
    /* launcher.txt, keys.txt, args.txt, config/keys.txt, config/settings.json,
     * the USB settings folder's one file, and SPEED2's three files beside it. */
    assert( made.files == 9 );
    assert( made.bytes > 0 );
    assert( exists( "runtime/backups/2026-10-26-1745/manifest.txt" ) );
    assert( exists( "runtime/backups/2026-10-26-1745/files/0000" ) );
    assert( exists( "runtime/backups/2026-10-26-1745/files/0008" ) );
    assert( !exists( "runtime/backups/2026-10-26-1745/files/0009" ) );
    read_text( "runtime/backups/2026-10-26-1745/manifest.txt", text, sizeof(text) );
    assert( strstr( text, "runtime/launcher.txt" ) );
    assert( strstr( text, "sdmc:/games/SPEED2.args.txt" ) );
    assert( !strstr( text, "USB.wine-nx.txt" ) );   /* its settings came with the folder */

    assert( launcher_backup_list( "runtime", kept, LAUNCHER_BACKUP_MAX, &count ) );
    assert( count == 1 && !strcmp( kept[0].name, "2026-10-26-1745" ) && kept[0].files == 9 );
    assert( launcher_backup_count( "runtime" ) == 1 );

    /* The same minute twice: the second one gets a suffix, and the list is
     * newest first. */
    assert( launcher_backup_create( "runtime", "2026-10-26-1745", programs, 3, 0, &made, error, sizeof(error) ) );
    assert( !strcmp( made.name, "2026-10-26-1745-2" ) );
    assert( launcher_backup_list( "runtime", kept, LAUNCHER_BACKUP_MAX, &count ) );
    assert( count == 2 && !strcmp( kept[0].name, "2026-10-26-1745-2" ) );
    assert( launcher_backup_count( "runtime" ) == 2 );
    assert( !strcmp( kept[1].name, "2026-10-26-1745" ) );

    /* Things change: a setting is edited, a file is deleted, a folder goes. */
    write_text( "runtime/launcher.txt", "show-hidden=1\n" );
    write_text( "sdmc:/games/SPEED2.wine-nx.txt", "cpu=box64\n" );
    remove( "sdmc:/games/SPEED2.args.txt" );
    remove_tree( "runtime/config" );

    assert( launcher_backup_restore( "runtime", "2026-10-26-1745", &restored, error, sizeof(error) ) );
    assert( restored == 9 );
    read_text( "runtime/launcher.txt", text, sizeof(text) );
    assert( !strcmp( text, "show-hidden=0\nhide-missing=1\n" ) );
    read_text( "sdmc:/games/SPEED2.wine-nx.txt", text, sizeof(text) );
    assert( !strcmp( text, "cpu=fex-opt\nframe-limit=30\n" ) );
    read_text( "sdmc:/games/SPEED2.args.txt", text, sizeof(text) );   /* put back where it was */
    assert( !strcmp( text, "-windowed\n" ) );
    read_text( "runtime/config/settings.json", text, sizeof(text) );  /* its folder made again */
    assert( strstr( text, "verbose-log" ) );
    read_text( "runtime/config/keys.txt", text, sizeof(text) );
    assert( !strcmp( text, "jump=b\n" ) );

    /* A name that is not one of ours cannot leave the folder. */
    assert( !launcher_backup_restore( "runtime", "../config", &restored, error, sizeof(error) ) );
    assert( error[0] );
    assert( !launcher_backup_restore( "runtime", "2026-10-27-0000", &restored, error, sizeof(error) ) );
    assert( error[0] );

    /* Five are kept: the two above and three more, with the oldest gone. */
    for (i = 0; i < LAUNCHER_BACKUP_MAX + 1; i++)
    {
        char stamp[LAUNCHER_BACKUP_NAME];

        snprintf( stamp, sizeof(stamp), "2026-11-01-120%d", i );
        assert( launcher_backup_create( "runtime", stamp, programs, 3, 0, &made, error, sizeof(error) ) );
    }
    assert( launcher_backup_list( "runtime", kept, LAUNCHER_BACKUP_MAX, &count ) );
    assert( count == LAUNCHER_BACKUP_MAX );
    assert( !strcmp( kept[0].name, "2026-11-01-1205" ) );
    assert( !exists( "runtime/backups/2026-10-26-1745" ) );
    assert( !exists( "runtime/backups/2026-10-26-1745-2" ) );
    assert( launcher_backup_count( "runtime" ) == LAUNCHER_BACKUP_MAX );

    /* Nothing to take: no settings have been written yet. */
    remove_tree( "runtime" );
    make_dirs( "runtime" );
    assert( !launcher_backup_create( "runtime", "2026-12-01-0000", no_programs, 0, 0, &made, error, sizeof(error) ) );
    assert( strstr( error, "no settings" ) );
    assert( !exists( "runtime/backups/2026-12-01-0000" ) );   /* a failed copy leaves nothing behind */
}

static void check_missing_places(void)
{
    static const char *const programs[] = { "sdmc:/games/SPEED2.EXE" };
    struct launcher_backup made, kept[2];
    char error[192], text[LAUNCHER_BACKUP_NAME];
    int count;

    /* A runtime folder that is not there yet: the copy says so with the path. */
    write_text( "runtime/launcher.txt", "show-hidden=0\n" );
    assert( launcher_backup_create( "runtime", "2026-01-02-0304", programs, 1, 0, &made, error, sizeof(error) ) );
    remove_tree( "runtime" );
    assert( !launcher_backup_create( "runtime", "2026-01-02-0304", programs, 1, 0, &made, error, sizeof(error) ) );
    assert( strstr( error, "runtime" ) );

    /* A list of no folder is a list of nothing, not a failure. */
    assert( launcher_backup_list( "runtime", kept, 2, &count ) && count == 0 );

    /* A name the module did not make is not listed, and the empty one is. */
    make_dirs( "runtime/backups/notes" );
    write_text( "runtime/backups/notes/manifest.txt", "0000\truntime/launcher.txt\n" );
    make_dirs( "runtime/backups/2026-01-01-0000" );
    write_text( "runtime/backups/2026-01-01-0000/manifest.txt", "0000\truntime/launcher.txt\n" );
    assert( launcher_backup_list( "runtime", kept, 2, &count ) );
    assert( count == 1 && !strcmp( kept[0].name, "2026-01-01-0000" ) );
    assert( kept[0].files == 1 );
    /* A folder the module did not make is not counted either. */
    assert( launcher_backup_count( "runtime" ) == 1 );
    assert( launcher_backup_count( "nowhere" ) == 0 );

    launcher_backup_stamp( text, sizeof(text), 1793036700u );
    assert( !strcmp( text, "2026-10-26-1745" ) );
}

/* A copy taken while another one is being read must not drop it, and the
 * keeping is what the caller says. */
static void check_keeping(void)
{
    static const char *const programs[] = { "sdmc:/games/SPEED2.EXE" };
    struct launcher_backup made, kept[LAUNCHER_BACKUP_MAX + 1];
    char error[192], stamp[LAUNCHER_BACKUP_NAME];
    int count, i;

    remove_tree( "runtime" );
    stage_card();
    for (i = 0; i < LAUNCHER_BACKUP_MAX + 1; i++)
    {
        snprintf( stamp, sizeof(stamp), "2026-12-02-120%d", i );
        assert( launcher_backup_create( "runtime", stamp, programs, 1, 0, &made, error, sizeof(error) ) );
    }
    assert( launcher_backup_list( "runtime", kept, LAUNCHER_BACKUP_MAX + 1, &count ) );
    assert( count == LAUNCHER_BACKUP_MAX );

    /* -1 keeps them all: the copy just taken and the four that were there. */
    assert( launcher_backup_create( "runtime", "2026-12-03-0900", programs, 1, -1, &made, error, sizeof(error) ) );
    assert( launcher_backup_list( "runtime", kept, LAUNCHER_BACKUP_MAX + 1, &count ) );
    assert( count == LAUNCHER_BACKUP_MAX + 1 );
    assert( launcher_backup_count( "runtime" ) == LAUNCHER_BACKUP_MAX + 1 );

    /* Pruning to five drops the oldest, which is the one an undo would read. */
    launcher_backup_prune( "runtime", LAUNCHER_BACKUP_MAX );
    assert( launcher_backup_list( "runtime", kept, LAUNCHER_BACKUP_MAX + 1, &count ) );
    assert( count == LAUNCHER_BACKUP_MAX );
    assert( !strcmp( kept[0].name, "2026-12-03-0900" ) );
    assert( !strcmp( kept[count - 1].name, "2026-12-02-1202" ) );
    assert( !exists( "runtime/backups/2026-12-02-1201" ) );

    /* A number to keep that is below what is there, and nothing to keep. */
    launcher_backup_prune( "runtime", 2 );
    assert( launcher_backup_count( "runtime" ) == 2 );
    launcher_backup_prune( "runtime", 0 );   /* 0 keeps LAUNCHER_BACKUP_MAX, and two are below it */
    assert( launcher_backup_count( "runtime" ) == 2 );
}

/* A manifest is a list of where to put files back. One that climbs out of its
 * folder, or one with nothing in it, is refused before anything is written. */
static void check_restore_refusals(void)
{
    static const char *const programs[] = { "sdmc:/games/SPEED2.EXE" };
    struct launcher_backup made;
    char error[192], text[1024];
    unsigned int restored;

    remove_tree( "runtime" );
    stage_card();
    assert( launcher_backup_create( "runtime", "2026-12-04-1000", programs, 1, 0, &made, error, sizeof(error) ) );
    assert( launcher_backup_restore( "runtime", "2026-12-04-1000", &restored, error, sizeof(error) ) );
    /* The launcher's five, the settings folder under the runtime, and the three
     * beside the game. */
    assert( restored == 9 );

    write_text( "runtime/backups/2026-12-04-1000/manifest.txt", "0000\t../escaped.txt\n" );
    write_text( "outside.txt", "kept\n" );
    assert( !launcher_backup_restore( "runtime", "2026-12-04-1000", &restored, error, sizeof(error) ) );
    assert( strstr( error, "may put back" ) );
    assert( restored == 0 );
    read_text( "outside.txt", text, sizeof(text) );
    assert( !strcmp( text, "kept\n" ) );   /* the file it pointed at is untouched */

    /* A file the manifest may put back, and then one it may not: the first
     * stays back, and the caller is told so it can read the settings again. */
    write_text( "runtime/backups/2026-12-04-1000/manifest.txt",
                "0000\truntime/launcher.txt\n0001\t../escaped.txt\n" );
    assert( !launcher_backup_restore( "runtime", "2026-12-04-1000", &restored, error, sizeof(error) ) );
    assert( strstr( error, "may put back" ) );
    assert( restored == 1 );

    write_text( "runtime/backups/2026-12-04-1000/manifest.txt", "" );
    assert( !launcher_backup_restore( "runtime", "2026-12-04-1000", &restored, error, sizeof(error) ) );
    assert( strstr( error, "no files" ) );
}

/* A folder that carries the name of a settings file is skipped, not copied and
 * not a reason to fail. */
static void check_odd_files(void)
{
    static const char *const programs[] = { "sdmc:/games/SPEED2.EXE" };
    struct launcher_backup made;
    char error[192];

    remove_tree( "runtime" );
    stage_card();
    assert( !remove( "sdmc:/games/SPEED2.args.txt" ) );
    assert( !mkdir( "sdmc:/games/SPEED2.args.txt", 0777 ) );
    assert( launcher_backup_create( "runtime", "2026-12-05-1100", programs, 1, 0, &made, error, sizeof(error) ) );
    assert( made.files == 8 );   /* launcher.txt, keys.txt, args.txt, two config, three beside the game */
    assert( !strstr( error, "Could not" ) );
}

int main(void)
{
    char folder[] = "/tmp/wine-nx-backup-XXXXXX", start[512];

    assert( getcwd( start, sizeof(start) ) );
    assert( mkdtemp( folder ) );
    assert( !chdir( folder ) );

    check_stamp();
    check_detail();
    check_create_and_restore();
    check_missing_places();
    check_keeping();
    check_restore_refusals();
    check_odd_files();

    assert( !chdir( start ) );
    remove_tree( folder );
    puts( "launcher backup: stamps, details, copies, manifest, restore and pruning passed" );
    return 0;
}
