/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#include "fex_cache.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FEX_CACHE_PATH 768
/* More configurations than this on one card is beyond what the cache is worth
 * reasoning about: what was found is pruned, the rest is left for next time. */
#define FEX_CACHE_BUCKETS 256
/* What one directory may hold before a removal gives up. */
#define FEX_CACHE_ATTEMPTS 4096

struct fex_cache_bucket
{
    char path[FEX_CACHE_PATH];
    unsigned long long bytes;
    long long mtime;
};

static int fex_cache_join( char *out, size_t size, const char *dir, const char *name )
{
    int length = snprintf( out, size, "%s/%s", dir, name );

    return length > 0 && (size_t)length < size;
}

static int fex_cache_skip( const char *name )
{
    return name[0] == '.' && (!name[1] || (name[1] == '.' && !name[2]));
}

/* What one configuration holds and when it was last written. A directory whose
 * files cannot be read counts as empty: it is not worth pruning. */
static void fex_cache_measure( struct fex_cache_bucket *bucket )
{
    struct dirent *entry;
    struct stat st;
    char path[FEX_CACHE_PATH];
    DIR *dir;

    if (!(dir = opendir( bucket->path ))) return;
    while ((entry = readdir( dir )))
    {
        if (fex_cache_skip( entry->d_name )) continue;
        if (!fex_cache_join( path, sizeof(path), bucket->path, entry->d_name )) continue;
        if (stat( path, &st ) || !S_ISREG( st.st_mode )) continue;
        bucket->bytes += (unsigned long long)st.st_size;
        if ((long long)st.st_mtime > bucket->mtime) bucket->mtime = (long long)st.st_mtime;
    }
    closedir( dir );
}

/* Gives back what is inside one directory. The listing is read, closed and
 * asked for again after each entry: taking an entry away while a listing is
 * being walked can make the next one be skipped, which on a card would leave
 * that file behind and the directory unable to go. */
static int fex_cache_empty( const char *path )
{
    unsigned int attempts = 0;

    while (attempts++ < FEX_CACHE_ATTEMPTS)
    {
        char name[256], child[FEX_CACHE_PATH];
        struct dirent *entry;
        struct stat st;
        int is_dir;
        DIR *dir;

        if (!(dir = opendir( path ))) return 0;
        name[0] = 0;
        while ((entry = readdir( dir )))
        {
            if (fex_cache_skip( entry->d_name )) continue;
            snprintf( name, sizeof(name), "%s", entry->d_name );
            break;
        }
        closedir( dir );
        if (!name[0]) return 1;                      /* nothing left */
        if (!fex_cache_join( child, sizeof(child), path, name )) return 0;
        is_dir = !lstat( child, &st ) && S_ISDIR( st.st_mode );
        if (is_dir)
        {
            if (!fex_cache_empty( child ) || rmdir( child )) return 0;
        }
        else if (unlink( child )) return 0;
    }
    return 0;
}

/* Takes a whole configuration away. Half of one is worth nothing: an index
 * without its database cannot be read, and the next run writes both again. */
static int fex_cache_remove( const char *path )
{
    return fex_cache_empty( path ) && !rmdir( path );
}

/* Whether `bucket` should be given up before `than`. */
static int fex_cache_older( const struct fex_cache_bucket *bucket, const struct fex_cache_bucket *than )
{
    if (bucket->mtime != than->mtime) return bucket->mtime < than->mtime;
    return strcmp( bucket->path, than->path ) < 0;
}

int fex_cache_prune( const char *root, unsigned long long limit, unsigned long long keep,
                     struct fex_cache_report *report )
{
    struct fex_cache_bucket *buckets = NULL;
    unsigned int count = 0, room = 0, i, j;
    unsigned long long total = 0;
    char cache[FEX_CACHE_PATH];
    struct dirent *entry;
    struct stat st;
    int stop = 0;
    DIR *dir;

    memset( report, 0, sizeof(*report) );
    if (!root || !root[0] || !fex_cache_join( cache, sizeof(cache), root, "DiskCache" )) return 1;
    if (!(dir = opendir( cache )))
    {
        report->failed = errno != ENOENT;
        return !report->failed;
    }
    while (!stop && (entry = readdir( dir )))
    {
        struct fex_cache_bucket bucket;

        if (fex_cache_skip( entry->d_name )) continue;
        memset( &bucket, 0, sizeof(bucket) );
        if (!fex_cache_join( bucket.path, sizeof(bucket.path), cache, entry->d_name )) continue;
        if (stat( bucket.path, &st ) || !S_ISDIR( st.st_mode )) continue;
        /* The age is the newest file the configuration holds, not the
         * directory's own time: a directory's time is when something in it
         * last changed, which for a cache that is being written is now. */
        fex_cache_measure( &bucket );
        if (!bucket.bytes) continue;   /* a directory the translator has not filled yet */
        if (count == room)
        {
            unsigned int grown = room ? room * 2 : 8;
            struct fex_cache_bucket *bigger;

            if (grown > FEX_CACHE_BUCKETS) break;
            if (!(bigger = realloc( buckets, grown * sizeof(*buckets) )))
            {
                stop = 1;
                break;
            }
            buckets = bigger;
            room = grown;
        }
        buckets[count++] = bucket;
        total += bucket.bytes;
    }
    closedir( dir );

    report->buckets = count;
    report->bytes = total;
    report->kept = total;
    if (total <= limit)
    {
        free( buckets );
        return 1;
    }
    /* Oldest first: the configuration nobody has run for months is the one to
     * give up. Two written in the same second are ordered by name, so the same
     * card gives up the same ones twice. */
    for (i = 1; i < count; i++)
        for (j = i; j && fex_cache_older( buckets + j, buckets + j - 1 ); j--)
        {
            struct fex_cache_bucket move = buckets[j];

            buckets[j] = buckets[j - 1];
            buckets[j - 1] = move;
        }
    report->pruned = 1;
    for (i = 0; i < count && report->kept > keep; i++)
    {
        if (!fex_cache_remove( buckets[i].path )) continue;
        report->kept -= buckets[i].bytes;
        report->buckets_removed++;
    }
    free( buckets );
    return 1;
}
