/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
/*
 * The JIT's translated code, kept on the card between runs.
 *
 * FEX 2609 gave the JIT an on-disk cache (FEX_DISKCACHE=1): every block it
 * translates is written to a database and looked up before translating again
 * on the next run. On a console whose core runs at a gigahertz, translating a
 * game's blocks is the slowest thing that happens in its first minutes, so a
 * second run that does not translate the same blocks again starts faster and
 * stutters less. Proton's ARM builds and the Linux distributions turn it on
 * for that reason.
 *
 * Two things the release does not do, and this does. The cache has no size
 * limit in 2609 (2610 added one), and applications that change their own code
 * can make it grow without end -- a 32 GB card is not the place to find out.
 * And its layout is not a promise: the cache is a directory per configuration
 * under DiskCache/, holding a database and its index, named after a hash of
 * the translator's settings and the host's features. Half of one of those is
 * worth nothing, so the sizes are measured per directory and the oldest whole
 * configurations are given back first, until what is left fits.
 *
 * Pure filesystem work over a path the caller owns, so tests/fex_cache.c can
 * drive it on the host.
 */
#ifndef WINE_NX_FEX_CACHE_H
#define WINE_NX_FEX_CACHE_H

#include <stddef.h>
#include <stdio.h>

/* FEX 2610's own default, which the console's card has room for. */
#define FEX_CACHE_LIMIT (1024ull * 1024 * 1024)
/* Cut back to this once the limit is crossed, so the next starts do not have
 * to prune again for a while. */
#define FEX_CACHE_KEEP  (768ull * 1024 * 1024)

struct fex_cache_report
{
    unsigned long long bytes;   /* what the cache held */
    unsigned long long kept;    /* what is left after the prune */
    unsigned int buckets;       /* configurations found */
    unsigned int buckets_removed;
    int pruned;                 /* 1 when the limit was crossed */
    int failed;                 /* 1 when DiskCache/ could not be read */
};

/* Where the module keeps the database, given the runtime's own directory
 * (sdmc:/switch/wine): under the user's profile, the local half and the
 * roaming one, because a module may put it in either. One function for the
 * runtime and for the launcher, so the two cannot drift apart. */
static inline int fex_cache_root( char *out, size_t size, const char *runtime_dir, int roaming )
{
    int length = snprintf( out, size, "%s/drive_c/users/steamuser/AppData/%s/fex-emu",
                           runtime_dir, roaming ? "Roaming" : "Local" );

    return length > 0 && (size_t)length < size;
}

/* Measures `root`/DiskCache and, when it holds more than `limit` bytes, takes
 * whole configurations away oldest first until `keep` bytes or less remain.
 * A root without a cache is not an error: nothing has been translated yet.
 * Returns 0 only when the cache is there but could not be read, so a caller
 * can say so once and carry on. */
int fex_cache_prune( const char *root, unsigned long long limit, unsigned long long keep,
                     struct fex_cache_report *report );

/* What the cache holds, without touching it: the launcher's line about it, and
 * the same measurement the prune starts from. 0 is returned only when the
 * cache is there and cannot be read. */
int fex_cache_measure( const char *root, unsigned long long *bytes, unsigned int *buckets );

/* Gives every configuration back -- the launcher's "clear it" -- and leaves the
 * directory for the translator to fill again. */
int fex_cache_clear( const char *root );

#endif
