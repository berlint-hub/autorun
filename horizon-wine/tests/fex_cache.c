/* Host test for the prune of FEX's translated code cache (source/fex_cache.c):
 * what counts as one configuration, which one goes first when the card has no
 * room, and that a card without a cache is left alone. */
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

#include "../source/fex_cache.h"

static char root[512];
static unsigned long long written;

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
    assert( (handle = fopen( file, "wb" )) );
    for (long i = 0; i < size; i++) fputc( 'x', handle );
    assert( !fclose( handle ) );
    assert( !utime( file, &stamp ) );
    written += (unsigned long long)size;
}

/* One configuration: the database and its index, as the translator leaves
 * them, plus what it costs on the card. */
static void make_bucket( const char *name, long size, time_t when )
{
    char dir[600];

    path_of( dir, sizeof(dir), name );
    assert( !mkdir( dir, 0777 ) );
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
    assert( !mkdir( root, 0777 ) );
    if (!with_cache) return;
    path_of( dir, sizeof(dir), NULL );
    assert( !mkdir( dir, 0777 ) );
    written = 0;
}

int main( void )
{
    struct fex_cache_report report;
    char template[] = "/tmp/wine-nx-fex-cache-XXXXXX";
    const time_t now = 1700000000;

    assert( mkdtemp( template ) );
    snprintf( root, sizeof(root), "%s", template );

    /* No cache yet: nothing measured, and no complaint about it. */
    reset_tree( 0 );
    assert( fex_cache_prune( root, FEX_CACHE_LIMIT, FEX_CACHE_KEEP, &report ) );
    assert( !report.buckets && !report.bytes && !report.pruned && !report.failed );

    /* A cache directory the translator has made but not filled. */
    reset_tree( 1 );
    assert( fex_cache_prune( root, FEX_CACHE_LIMIT, FEX_CACHE_KEEP, &report ) );
    assert( !report.buckets && !report.pruned && !report.failed );

    /* Three configurations, 101, 201 and 301 bytes, the newest written last.
     * Their names sort the other way round, so a prune that went by name would
     * give up the wrong one. */
    reset_tree( 1 );
    make_bucket( "aa", 100, now - 3000 );
    make_bucket( "zz", 200, now - 2000 );
    make_bucket( "mm", 300, now - 1000 );
    assert( written == 603 );

    /* Under the limit: measured, nothing taken. */
    assert( fex_cache_prune( root, 1000, 500, &report ) );
    assert( report.buckets == 3 && report.bytes == 603 && report.kept == 603 );
    assert( !report.pruned && !report.buckets_removed );

    /* Over it: the oldest goes, then the next, until what is left fits -- and
     * never a file out of a configuration. */
    assert( fex_cache_prune( root, 450, 350, &report ) );
    assert( report.pruned && report.buckets == 3 && report.bytes == 603 );
    assert( report.buckets_removed == 2 && report.kept == 301 );
    assert( !bucket_exists( "aa" ) && !bucket_exists( "zz" ) && bucket_exists( "mm" ) );

    /* What fits is looked at again on the next start and left alone, and the
     * cache's own directory is still there for the translator to fill. */
    assert( fex_cache_prune( root, 450, 350, &report ) );
    assert( !report.pruned && report.buckets == 1 && report.kept == 301 );
    assert( !report.failed && !report.buckets_removed && cache_exists() );

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
    assert( written == 802 );
    assert( fex_cache_prune( root, 700, 650, &report ) );
    assert( report.buckets == 2 && report.bytes == 802 && report.buckets_removed == 1 );
    assert( report.kept == 201 && !bucket_exists( "aa" ) && bucket_exists( "zz" ) );

    /* A root that is not there is not a fault; the cache is simply not made
     * yet, which is what every first run looks like. */
    assert( fex_cache_prune( "/nonexistent/wine-nx", 400, 100, &report ) );
    assert( !report.failed && !report.buckets );

    remove_tree( root );

    printf( "fex_cache: ok\n" );
    return 0;
}
