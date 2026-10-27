/* Host test for the prune of FEX's translated code cache (source/fex_cache.c):
 * what counts as one configuration, which one goes first when the card has no
 * room, and that a card without a cache is left alone. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

#include "../source/fex_cache.h"

#define FEX_CACHE_PATH_TEST 128

static char root[512];
static char detail[256];
static unsigned long long written;

/* The last prune's numbers, so a failure says which one was not what the case
 * asked for. */
static void note_report( const struct fex_cache_report *report )
{
    snprintf( detail, sizeof(detail), " [buckets %u, bytes %llu, kept %llu, removed %u, pruned %d, failed %d]",
              report->buckets, report->bytes, report->kept, report->buckets_removed,
              report->pruned, report->failed );
}

/* A failed case is named rather than left as a bare signal: the runner shows
 * the line as an annotation on the commit, which is where a failure in a
 * filesystem this test cannot see is worth reading. */
static void check_failed( int line, const char *what )
{
    char path[512];
    FILE *log;

    printf( "::error file=horizon-wine/tests/fex_cache.c,line=%d::%s%s\n", line, what, detail );
    fflush( stdout );
    mkdir( "shots", 0777 );
    snprintf( path, sizeof(path), "shots/fex_cache-failure.txt" );
    if ((log = fopen( path, "w" )))
    {
        fprintf( log, "line %d: %s%s\n", line, what, detail );
        fclose( log );
    }
    abort();
}

#define CHECK(x) do { if (!(x)) check_failed( __LINE__, #x ); } while (0)

static void path_of( char *out, size_t size, const char *name )
{
    if (name) snprintf( out, size, "%s/DiskCache/%s", root, name );
    else snprintf( out, size, "%s/DiskCache", root );
}

/* A file of `size` bytes, written when it is said to have been written. */
static void make_file( const char *dir, const char *name, long size, time_t when )
{
    char file[700];
    struct utimbuf stamp = { when, when };
    FILE *handle;

    snprintf( file, sizeof(file), "%s/%s", dir, name );
    CHECK( (handle = fopen( file, "wb" )) );
    for (long i = 0; i < size; i++) fputc( 'x', handle );
    CHECK( !fclose( handle ) );
    CHECK( !utime( file, &stamp ) );
    written += (unsigned long long)size;
}

/* One configuration: the database and its index, as the translator leaves
 * them, plus what it costs on the card. */
static void make_bucket( const char *name, long size, time_t when )
{
    char dir[600];

    path_of( dir, sizeof(dir), name );
    CHECK( !mkdir( dir, 0777 ) );
    make_file( dir, "RWCacheDB.foz", size, when );
    make_file( dir, "RWCacheDB_idx.foz", 1, when );
}

static int bucket_exists( const char *name )
{
    char dir[600];

    path_of( dir, sizeof(dir), name );
    return !access( dir, F_OK );
}

static int cache_exists( void )
{
    char dir[600];

    path_of( dir, sizeof(dir), NULL );
    return !access( dir, F_OK );
}

/* The listing is closed and asked for again after each entry: removing one
 * while a listing is being walked can make the next be skipped, which is the
 * sort of thing the prune itself has to cope with on a card. */
static void remove_tree( const char *path )
{
    for (;;)
    {
        char name[256], child[700];
        struct stat st;
        struct dirent *entry;
        DIR *dir;

        if (lstat( path, &st )) return;
        if (!S_ISDIR( st.st_mode ) || !(dir = opendir( path )))
        {
            unlink( path );
            return;
        }
        name[0] = 0;
        while ((entry = readdir( dir )))
        {
            if (!strcmp( entry->d_name, "." ) || !strcmp( entry->d_name, ".." )) continue;
            snprintf( name, sizeof(name), "%s", entry->d_name );
            break;
        }
        closedir( dir );
        if (!name[0]) break;
        snprintf( child, sizeof(child), "%s/%s", path, name );
        remove_tree( child );
    }
    rmdir( path );
}

/* A card before the translator has run: the runtime's directory, with no cache
 * in it. */
static void reset_tree( int with_cache )
{
    char dir[600];

    remove_tree( root );
    CHECK( !mkdir( root, 0777 ) );
    if (!with_cache) return;
    path_of( dir, sizeof(dir), NULL );
    CHECK( !mkdir( dir, 0777 ) );
    written = 0;
}

int main( void )
{
    struct fex_cache_report report;
    char template[] = "/tmp/wine-nx-fex-cache-XXXXXX";
    const time_t now = 1700000000;

    CHECK( mkdtemp( template ) );
    snprintf( root, sizeof(root), "%s", template );

    /* No cache yet: nothing measured, and no complaint about it. */
    reset_tree( 0 );
    CHECK( fex_cache_prune( root, FEX_CACHE_LIMIT, FEX_CACHE_KEEP, &report ) );
    note_report( &report );
    CHECK( !report.buckets && !report.bytes && !report.pruned && !report.failed );

    /* A cache directory the translator has made but not filled. */
    reset_tree( 1 );
    CHECK( fex_cache_prune( root, FEX_CACHE_LIMIT, FEX_CACHE_KEEP, &report ) );
    note_report( &report );
    CHECK( !report.buckets && !report.pruned && !report.failed );

    /* Three configurations, 101, 201 and 301 bytes, the newest written last.
     * Their names sort the other way round, so a prune that went by name would
     * give up the wrong one. */
    reset_tree( 1 );
    make_bucket( "aa", 100, now - 3000 );
    make_bucket( "zz", 200, now - 2000 );
    make_bucket( "mm", 300, now - 1000 );
    CHECK( written == 603 );

    /* Under the limit: measured, nothing taken. */
    CHECK( fex_cache_prune( root, 1000, 500, &report ) );
    note_report( &report );
    CHECK( report.buckets == 3 && report.bytes == 603 && report.kept == 603 );
    CHECK( !report.pruned && !report.buckets_removed );

    /* Over it: the oldest goes, then the next, until what is left fits -- and
     * never a file out of a configuration. */
    CHECK( fex_cache_prune( root, 450, 350, &report ) );
    note_report( &report );
    CHECK( report.pruned && report.buckets == 3 && report.bytes == 603 );
    CHECK( report.buckets_removed == 2 && report.kept == 301 );
    CHECK( !bucket_exists( "aa" ) && !bucket_exists( "zz" ) && bucket_exists( "mm" ) );

    /* What fits is looked at again on the next start and left alone, and the
     * cache's own directory is still there for the translator to fill. */
    CHECK( fex_cache_prune( root, 450, 350, &report ) );
    note_report( &report );
    CHECK( !report.pruned && report.buckets == 1 && report.kept == 301 );
    CHECK( !report.failed && !report.buckets_removed && cache_exists() );

    /* A configuration with something else in it is still one configuration,
     * measured with it and given back whole. */
    reset_tree( 1 );
    make_bucket( "aa", 100, now - 3000 );
    {
        char dir[600];

        path_of( dir, sizeof(dir), "aa" );
        make_file( dir, "notes.bin", 500, now - 3000 );
    }
    make_bucket( "zz", 200, now - 2000 );
    CHECK( written == 802 );
    CHECK( fex_cache_prune( root, 700, 650, &report ) );
    note_report( &report );
    CHECK( report.buckets == 2 && report.bytes == 802 && report.buckets_removed == 1 );
    CHECK( report.kept == 201 && !bucket_exists( "aa" ) && bucket_exists( "zz" ) );

    /* A configuration's age is the newest file it holds, not the directory it
     * is in: writing anything into a directory makes the directory look as if
     * it had just been written, and a prune that believed that would never
     * find the oldest configuration on a card. */
    reset_tree( 1 );
    make_bucket( "zz", 100, now - 3000 );              /* 101 bytes, old */
    make_bucket( "aa", 200, now - 1000 );              /* 201 bytes, newer */
    {
        char dir[600];
        struct utimbuf stamp = { now, now };

        path_of( dir, sizeof(dir), "zz" );
        CHECK( !utime( dir, &stamp ) );                /* the directory is "now" */
    }
    CHECK( fex_cache_prune( root, 250, 250, &report ) );
    note_report( &report );
    CHECK( report.buckets == 2 && report.bytes == 302 && report.buckets_removed == 1 );
    CHECK( report.kept == 201 && !bucket_exists( "zz" ) && bucket_exists( "aa" ) );

    /* Two written in the same second go in the order their names sort, so the
     * same card gives up the same ones twice whatever its listing says. */
    reset_tree( 1 );
    make_bucket( "bb", 100, now - 2000 );              /* 101 bytes, made first */
    make_bucket( "aa", 100, now - 2000 );              /* 101 bytes, same second */
    CHECK( fex_cache_prune( root, 150, 150, &report ) );
    note_report( &report );
    CHECK( report.buckets == 2 && report.bytes == 202 && report.buckets_removed == 1 );
    CHECK( report.kept == 101 && !bucket_exists( "aa" ) && bucket_exists( "bb" ) );

    /* Measuring is reading: the launcher's line about the cache must not take
     * anything away, and the same bytes the prune starts from are what it
     * says. */
    reset_tree( 1 );
    make_bucket( "aa", 100, now - 3000 );
    make_bucket( "zz", 200, now - 1000 );
    {
        unsigned long long bytes = 0;
        unsigned int buckets = 0;

        CHECK( fex_cache_measure( root, &bytes, &buckets ) );
        note_report( &report );
        CHECK( bytes == written && buckets == 2 );
        CHECK( bucket_exists( "aa" ) && bucket_exists( "zz" ) );
        /* And a cache that is not there measures as nothing, not as a fault. */
        CHECK( fex_cache_measure( "/nonexistent/wine-nx", &bytes, &buckets ) );
        CHECK( !bytes && !buckets );
    }

    /* Clearing gives every configuration back and leaves the directory the
     * translator fills; a second clear finds nothing and is not a fault. */
    CHECK( fex_cache_clear( root ) );
    CHECK( !bucket_exists( "aa" ) && !bucket_exists( "zz" ) && cache_exists() );
    CHECK( fex_cache_clear( root ) );
    CHECK( fex_cache_clear( "/nonexistent/wine-nx" ) );

    /* A tree of the shape Mesa's shader cache has on the card: a directory per
     * driver version, the driver's own one below it. The same limit applies,
     * files two levels down are what a version costs, and a version goes back
     * whole. */
    {
        char tree[600], version[700], driver[800];
        struct fex_cache_report shaders;
        unsigned long long bytes;
        unsigned int versions;

        snprintf( tree, sizeof(tree), "%s/mesa_shader_cache_sf", root );
        CHECK( !mkdir( tree, 0777 ) );
        /* The older version is the bigger one, so a prune that went by size
         * instead of age would give up the one still being written. */
        snprintf( version, sizeof(version), "%s/6e766b2d3236", tree );
        CHECK( !mkdir( version, 0777 ) );
        snprintf( driver, sizeof(driver), "%s/nvk_012b", version );
        CHECK( !mkdir( driver, 0777 ) );
        make_file( driver, "foz_cache.foz", 400, now - 3000 );
        make_file( driver, "foz_cache_idx.foz", 1, now - 3000 );
        snprintf( version, sizeof(version), "%s/6e766b2d3237", tree );
        CHECK( !mkdir( version, 0777 ) );
        snprintf( driver, sizeof(driver), "%s/nvk_012b", version );
        CHECK( !mkdir( driver, 0777 ) );
        make_file( driver, "foz_cache.foz", 100, now - 1000 );

        CHECK( fex_cache_measure_tree( tree, &bytes, &versions ) );
        CHECK( bytes == 501 && versions == 2 );
        CHECK( fex_cache_prune_tree( tree, 1000, 500, &shaders ) );
        note_report( &shaders );
        CHECK( !shaders.pruned && shaders.kept == 501 && shaders.buckets_removed == 0 );

        CHECK( fex_cache_prune_tree( tree, 450, 350, &shaders ) );
        note_report( &shaders );
        CHECK( shaders.pruned && shaders.buckets_removed == 1 && shaders.kept == 100 );
        /* One driver version is gone whole, the other is untouched. */
        CHECK( !access( tree, F_OK ) );
        snprintf( driver, sizeof(driver), "%s/6e766b2d3236/nvk_012b/foz_cache.foz", tree );
        CHECK( access( driver, F_OK ) );
        snprintf( driver, sizeof(driver), "%s/6e766b2d3237/nvk_012b/foz_cache.foz", tree );
        CHECK( !access( driver, F_OK ) );

        /* Everything given back, the directory left for the driver. */
        CHECK( fex_cache_clear_tree( tree ) );
        CHECK( fex_cache_measure_tree( tree, &bytes, &versions ) );
        CHECK( !bytes && !versions && !access( tree, F_OK ) );

        /* A cache the driver has not filled yet is not a fault. */
        CHECK( fex_cache_prune_tree( "/nonexistent/wine-nx/mesa_shader_cache_sf", 400, 100, &shaders ) );
        note_report( &shaders );
        CHECK( !shaders.failed && !shaders.buckets );
    }

    /* The path the runtime and the launcher both name: the module's database
     * is under the user profile, local or roaming. */
    {
        char path[FEX_CACHE_PATH_TEST];

        CHECK( fex_cache_root( path, sizeof(path), "sdmc:/switch/wine", 0 ) );
        CHECK( !strcmp( path, "sdmc:/switch/wine/drive_c/users/steamuser/AppData/Local/fex-emu" ) );
        CHECK( fex_cache_root( path, sizeof(path), "sdmc:/switch/wine", 1 ) );
        CHECK( !strcmp( path, "sdmc:/switch/wine/drive_c/users/steamuser/AppData/Roaming/fex-emu" ) );

        /* What the module is told (FEX_DISKCACHEPATH): one directory of its
         * own per build, in the form its Windows file calls understand. The
         * trailing separator is the module's, which appends its database's
         * file names to it. */
        CHECK( fex_cache_database_path( path, sizeof(path), "nx-amd64-fex-2640-opt" ) );
        CHECK( !strcmp( path, "C:\\users\\steamuser\\AppData\\Local\\fex-emu\\DiskCache\\"
                              "nx-amd64-fex-2640-opt\\" ) );
        /* Two builds do not read each other's translations. */
        CHECK( fex_cache_database_path( path, sizeof(path), "nx-amd64-fex-2640-stock" ) );
        CHECK( !strcmp( path, "C:\\users\\steamuser\\AppData\\Local\\fex-emu\\DiskCache\\"
                              "nx-amd64-fex-2640-stock\\" ) );
        /* A build string is not a path. */
        CHECK( fex_cache_database_path( path, sizeof(path), "x86/../y z" ) );
        CHECK( !strcmp( path, "C:\\users\\steamuser\\AppData\\Local\\fex-emu\\DiskCache\\"
                              "x86_.._y_z\\" ) );
        /* Nothing to name it after, nowhere to put it. */
        CHECK( !fex_cache_database_path( path, sizeof(path), "" ) );
        CHECK( !fex_cache_database_path( path, 16, "nx-amd64-fex-2640-opt" ) );
        /* It names a directory the measurement above can reach: the launcher
         * counts C:\users\steamuser\AppData\Local as drive_c/users/steamuser/
         * AppData/Local under the runtime's folder. */
        CHECK( !strncmp( FEX_CACHE_WINDOWS_ROOT,
                         "C:\\users\\steamuser\\AppData\\Local\\fex-emu\\DiskCache\\", 57 ) );
    }

    /* The marker of a start that is under way: two programs have two markers,
     * a start that comes back takes its own away, and one that does not come
     * back is found again -- which is what turns the cache off next time. */
    {
        char one[FEX_CACHE_PATH_TEST * 2], two[FEX_CACHE_PATH_TEST * 2];
        char other[FEX_CACHE_PATH_TEST * 2];

        CHECK( fex_cache_marker_path( one, sizeof(one), "sdmc:/switch/wine", "sdmc:/switch/wine/drive_c/Games/A/game.exe" ) );
        CHECK( fex_cache_marker_path( two, sizeof(two), "sdmc:/switch/wine", "sdmc:/switch/wine/drive_c/Games/A/game.exe" ) );
        CHECK( !strcmp( one, two ) );
        CHECK( fex_cache_marker_path( other, sizeof(other), "sdmc:/switch/wine", "sdmc:/switch/wine/drive_c/Games/B/game.exe" ) );
        CHECK( strcmp( one, other ) );
        /* The same program named with the other slash is the same program. */
        CHECK( fex_cache_marker_path( two, sizeof(two), "sdmc:/switch/wine", "sdmc:\\switch\\wine\\drive_c\\Games\\A\\game.exe" ) );
        CHECK( !strcmp( one, two ) );
        /* Nowhere to put it, nothing to write. */
        CHECK( !fex_cache_marker_path( one, sizeof(one), "", "x.exe" ) );
        CHECK( !fex_cache_marker_path( one, 8, "sdmc:/switch/wine", "x.exe" ) );

        /* A start that comes back leaves nothing behind. The runtime makes
         * program-settings before it writes a marker; here it is made too. */
        {
            char folder[700];

            snprintf( folder, sizeof(folder), "%s/program-settings", root );
            CHECK( !mkdir( folder, 0777 ) );
        }
        CHECK( fex_cache_marker_path( one, sizeof(one), root, "game.exe" ) );
        CHECK( fex_cache_marker_clear( one ) );           /* nothing there yet */
        CHECK( fex_cache_marker_set( one ) );
        CHECK( fex_cache_marker_clear( one ) );
        CHECK( !fex_cache_marker_take( one ) );

        /* A start that does not come back is found by the next one, once. */
        CHECK( fex_cache_marker_set( one ) );
        CHECK( fex_cache_marker_take( one ) );
        CHECK( !fex_cache_marker_take( one ) );
    }

    /* A root that is not there is not a fault; the cache is simply not made
     * yet, which is what every first run looks like. */
    CHECK( fex_cache_prune( "/nonexistent/wine-nx", 400, 100, &report ) );
    note_report( &report );
    CHECK( !report.failed && !report.buckets );

    remove_tree( root );

    printf( "fex_cache: ok\n" );
    return 0;
}
