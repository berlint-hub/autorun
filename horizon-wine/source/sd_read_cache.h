/*
 * A read cache for files on the SD card, in front of libnx's sdmc device.
 *
 * libnx turns each read() into an fsFileRead request to the FS service. Horizon
 * has no page cache or file mapping, and libnx has no file data cache (Nintendo's
 * SDK keeps one in its fs client library). OpenTTD reads its graphics with a
 * seek and a small read per sprite, so most requests fetch data that a nearby
 * read already had. As Dolphin's SectorReader does for disc images, each file
 * keeps a few chunks: a read inside one is a copy, a miss reads a chunk in one
 * request, and the least recently used chunk is replaced.
 *
 * How much a miss reads follows the file, as an operating system's readahead
 * does: fill_min at first, twice the last fill while the file is read in
 * order, back to fill_min when a read lands elsewhere. Reading 128 KB on
 * every miss cost The Sims 2 eight bytes from the card for every byte it
 * asked for, since it reads scattered records out of large packages, and at
 * the card's 37 MB/s the transfer, not the request, was most of each miss.
 * On the hardware it did not pay: 16 KB fills halved the bytes but nearly
 * tripled the requests, at 0.83 ms each, and the game rereads nearby data more
 * than the replay in tests/sd_read_cache.c does, so the runtime keeps whole
 * chunks (fill_min 0) and this stays for a pool that wants it.
 *
 * A file is cached whatever it was opened for, and a write to a path throws
 * away what every open file with that path holds (sd_cache_written), so the
 * next read of it goes to the card. Refusing to cache anything opened for
 * writing instead left The Sims 2 reading almost all of its packages from the
 * card one kilobyte at a time: it opens them for writing and then only reads.
 * A writer that cannot be recorded, whose writes would go unnoticed, still
 * stops the cache for good. The caller locks around these functions.
 */
#ifndef WINE_NX_SD_READ_CACHE_H
#define WINE_NX_SD_READ_CACHE_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define SD_CACHE_CHUNK  (128 * 1024)  /* the most a miss reads, and a line's buffer */
#define SD_CACHE_FILL_MIN (16 * 1024) /* what a miss reads until a file is read in order */
/* Chunks one open file may keep. A game that streams from a few big packages
 * rereads far more of one file than of the whole card, and eight chunks (1 MB)
 * held a few seconds of its streaming; twenty-four is 3 MB a file, and the pool
 * is still what bounds the total.
 *
 * Twenty-four is now the *smallest* a file gets, not the most. It is the right
 * number for a pool with only 32 MB in it, and it is badly wrong for a game
 * that streams its levels out of three or four packages while a gigabyte of
 * the heap sits idle: Dark Souls II held 30 MB of a 417 MB pool -- ten open
 * files on their 3 MB each -- served half of its file reads from the card,
 * pulled 1519 MB off it to answer 539 MB of requests, and dropped from about
 * 59 to under 45 fps in exactly the ten-second windows in which it was
 * reading. So the cap follows the pool: a file may keep a share of what the
 * pool has room for, up to the array below, and never less than the old
 * twenty-four. The pool's own limit still bounds the total, and its
 * least-recently-used chunk is still the one that goes when it is full, so a
 * file only ever holds what it has been reading.
 *
 * Ninety-six was tried first and is too small to be worth the change. Four
 * packages of half a gigabyte, read through a window that moves forward and
 * comes back over what it passed, ask the card for this much per file cap:
 *
 *   window   24 (3 MB)   96 (12 MB)   192 (24 MB)   384 (48 MB)
 *    8 MB      4747 MB        --           190 MB        --     (0.1x the bytes asked)
 *   16 MB      6112 MB     3788 MB         220 MB       220 MB
 *   64 MB      7136 MB     6359 MB        5392 MB      4733 MB
 *
 * Below the window a game streams in, more cache buys almost nothing; above
 * it, the card goes quiet. Dark Souls II pulls 1519 MB for 539 MB asked, which
 * puts its window between the last two rows, so 192 is where the curve turns
 * for it and 96 is not. The array is 40 bytes a slot: 7.7 KB per open file,
 * 2.3 MB at three hundred of them, next to a pool of hundreds of megabytes. */
#define SD_CACHE_LINES  192           /* the most chunks one file may keep */
#define SD_CACHE_LINES_MIN 24         /* what a file keeps when the pool is at its smallest */
#define SD_CACHE_LINES_SHARE 16       /* a file may take a pool's sixteenth, so sixteen files can fill up */
/* Reads up to this go through the cache: a game reading level data in 128 KB
 * or 256 KB pieces had every one of them go straight to the card and never
 * stay, so a level re-entered a minute later was read again in full. What is
 * larger than this is usually read once, and one request for it is cheaper
 * than the chunks it would fill. */
#define SD_CACHE_DIRECT (256 * 1024)  /* reads this large go straight to the file */
#define SD_CACHE_BYPASS (-2)          /* sd_cache_read found no memory: read directly */
#define SD_CACHE_POOL_MIN 256         /* 32 MB, the chunks every game gets */
/* A console playing a game of the 2010s leaves most of its memory untouched
 * (Dark Souls II sits at a sixth of it), and the card is the slowest thing in
 * the run: the cache is worth far more than the idle megabytes are. Half of
 * what is free above the game's floor, up to this. */
#define SD_CACHE_POOL_MAX 4096        /* 512 MB, when a game leaves that much heap free */
#define SD_CACHE_FREE_FLOOR_MB 512    /* the game's own memory, never taken */
#define SD_CACHE_FREE_SHARE 2         /* of what is free above the floor */

struct sd_cache_line
{
    char *data;
    long long start;   /* file offset of data[0], or -1 while it holds nothing */
    size_t len;        /* bytes held; less than a chunk only at the end of the file */
    unsigned int lru;  /* high bit set on use, shifted down on every miss */
    unsigned long long used_at;  /* the pool's clock when last read */
};

/* When every chunk is in use, a file takes the one read longest ago by any
 * file. Letting a full pool refuse new files instead left everything the
 * first few files had taken with them for as long as they were open: The
 * Sims 2 keeps hundreds of packages open, filled the pool in its first
 * seconds, and read every other package from the card for the rest of the
 * run -- about eight reads a chunk while there was room, then 149,000 card
 * reads in five minutes. held lists the lines holding a chunk.
 *
 * max is how many chunks the pool may hold, and the runtime moves it with the
 * heap the game leaves free (sd_cache.c): 32 MB is little next to a collection
 * whose packages are read all run long, and The Sims 2 plays with about a
 * gigabyte of the heap untouched. cap is what held can list, so max can grow
 * without the list moving. */
struct sd_cache_pool
{
    unsigned int used, max;        /* chunks allocated for all files, and the limit */
    unsigned int cap;              /* how many held can list; max never passes it */
    unsigned int fill_min;         /* the smallest fill, or 0 for whole chunks */
    unsigned long long clock;      /* counts every chunk read, across files */
    struct sd_cache_line **held;   /* the lines holding a chunk, used of them */
};

static inline void sd_cache_held_add( struct sd_cache_pool *pool, struct sd_cache_line *line )
{
    pool->held[pool->used++] = line;
}

static inline void sd_cache_held_remove( struct sd_cache_pool *pool, struct sd_cache_line *line )
{
    unsigned int i;

    for (i = 0; i < pool->used; i++)
    {
        if (pool->held[i] != line) continue;
        pool->held[i] = pool->held[--pool->used];
        break;
    }
    if (!pool->used)
    {
        free( pool->held );
        pool->held = NULL;
    }
}

/* A chunk changes hands without the count changing: removing it first would
 * free the list when it held the only one. */
static inline void sd_cache_held_replace( struct sd_cache_pool *pool, struct sd_cache_line *old,
                                          struct sd_cache_line *line )
{
    unsigned int i;

    for (i = 0; i < pool->used; i++)
    {
        if (pool->held[i] != old) continue;
        pool->held[i] = line;
        return;
    }
}

/* The chunk read longest ago, by any file. */
static inline struct sd_cache_line *sd_cache_oldest( struct sd_cache_pool *pool )
{
    struct sd_cache_line *oldest = NULL;
    unsigned int i;

    for (i = 0; i < pool->used; i++)
        if (!oldest || pool->held[i]->used_at < oldest->used_at) oldest = pool->held[i];
    return oldest;
}

/* Give chunks back, the one read longest ago first, until the pool holds no
 * more than max. The runtime lowers max as the game's own allocations take
 * the heap: a cache that kept its chunks would be taking memory from the
 * program it is there to speed up. */
static inline void sd_cache_trim( struct sd_cache_pool *pool )
{
    while (pool->used > pool->max)
    {
        struct sd_cache_line *old = sd_cache_oldest( pool );

        if (!old) break;
        free( old->data );
        old->data = NULL;
        old->start = -1;
        old->len = 0;
        old->lru = 0;
        sd_cache_held_remove( pool, old );
    }
}

static inline size_t sd_cache_reclaim( struct sd_cache_pool *pool, size_t bytes )
{
    size_t count = bytes / SD_CACHE_CHUNK + !!(bytes % SD_CACHE_CHUNK);
    unsigned int before = pool->used;

    if (count > before) count = before;
    pool->max = before - (unsigned int)count;
    sd_cache_trim( pool );
    return (size_t)(before - pool->used) * SD_CACHE_CHUNK;
}

/* How many chunks the pool may hold, given the megabytes of heap a game leaves
 * free and the chunks already held (which count as used heap, and are the
 * cache's own). The game's floor comes off the top, half of what is left is
 * the cache's, and the answer stays between the floor every game gets and the
 * ceiling. */
static inline unsigned int sd_cache_target_chunks( unsigned long long free_mb, unsigned int used_chunks )
{
    unsigned long long room = free_mb + (((unsigned long long)used_chunks * SD_CACHE_CHUNK) >> 20);
    unsigned long long chunks;

    if (room <= SD_CACHE_FREE_FLOOR_MB) return SD_CACHE_POOL_MIN;
    chunks = (room - SD_CACHE_FREE_FLOOR_MB) / SD_CACHE_FREE_SHARE * (1024 * 1024 / SD_CACHE_CHUNK);
    if (chunks < SD_CACHE_POOL_MIN) return SD_CACHE_POOL_MIN;
    if (chunks > SD_CACHE_POOL_MAX) return SD_CACHE_POOL_MAX;
    return (unsigned int)chunks;
}

struct sd_cache_file
{
    void *key;         /* libnx's per-open file data */
    char *path;
    int writable;
    int append;        /* opened with O_APPEND: every write lands at the end */
    int shared;        /* the path was open more than once at some point */
    unsigned int direct_writes;  /* writes that went straight to the card */
    int cacheable;
    unsigned int window;     /* the size of the last fill, 0 before the first */
    long long fill_end;      /* where the last fill ended: a miss there reads on in order */
    int size_known;          /* a fill came back short: the file ends at size */
    long long size;
    struct sd_cache_line lines[SD_CACHE_LINES];
    struct sd_cache_file *next;
};

/* Reads size bytes at offset into buf. Returns the bytes read or -1. */
typedef long long (*sd_cache_fill_fn)( void *ctx, long long offset, char *buf, size_t size );

static inline void sd_cache_drop( struct sd_cache_file *file, struct sd_cache_pool *pool )
{
    unsigned int i;

    for (i = 0; i < SD_CACHE_LINES; i++)
    {
        if (!file->lines[i].data) continue;
        free( file->lines[i].data );
        sd_cache_held_remove( pool, &file->lines[i] );
        memset( &file->lines[i], 0, sizeof(file->lines[i]) );
    }
    /* A write may have moved the end, and the order reads come in. */
    file->window = 0;
    file->fill_end = 0;
    file->size_known = 0;
}

static inline struct sd_cache_line *sd_cache_line_for( struct sd_cache_file *file, long long offset )
{
    unsigned int i;

    for (i = 0; i < SD_CACHE_LINES; i++)
    {
        struct sd_cache_line *line = &file->lines[i];

        if (line->data && line->start >= 0 && offset >= line->start &&
            offset < line->start + (long long)line->len)
        {
            line->lru |= 0x80000000u;
            return line;
        }
    }
    return NULL;
}

/* Where the next fill starts and how much it reads, for a miss at offset. It
 * starts at a fill_min boundary, so small reads next to one another share it,
 * and stops short of a line that already holds what follows. Returns 0 at or
 * past a known end of the file. */
static inline int sd_cache_plan( const struct sd_cache_file *file, const struct sd_cache_pool *pool,
                                 long long offset, long long *start, size_t *size, unsigned int *window )
{
    unsigned int step = pool->fill_min ? pool->fill_min : SD_CACHE_CHUNK, i;
    long long end;

    if (file->size_known && offset >= file->size) return 0;
    if (!pool->fill_min) *window = SD_CACHE_CHUNK;
    else if (file->window && offset == file->fill_end)  /* read on in order */
        *window = file->window * 2 > SD_CACHE_CHUNK ? SD_CACHE_CHUNK : file->window * 2;
    else *window = pool->fill_min;
    *start = offset - offset % step;
    end = *start + *window;
    if (end - offset < 1) end = offset + 1;
    for (i = 0; i < SD_CACHE_LINES; i++)
    {
        const struct sd_cache_line *line = &file->lines[i];

        if (line->data && line->start > offset && line->start < end) end = line->start;
    }
    if (file->size_known && end > file->size) end = file->size;
    *size = (size_t)(end - *start);
    return 1;
}

/* How many chunks one file may keep, given what the pool can hold: a share of
 * it, never more than the array, never less than the old twenty-four. A pool
 * at its 32 MB floor answers 16, which the floor raises back to 24, so a game
 * on a small pool caches exactly as much as it did before; a pool of 192 MB or
 * more lets a file keep the whole 96. */
static inline unsigned int sd_cache_file_lines( const struct sd_cache_pool *pool )
{
    unsigned int lines = pool->max / SD_CACHE_LINES_SHARE;

    if (lines > SD_CACHE_LINES) lines = SD_CACHE_LINES;
    if (lines < SD_CACHE_LINES_MIN) lines = SD_CACHE_LINES_MIN;
    return lines;
}

/* The line to fill: a buffer holding nothing, then a free slot, then the
 * least recently used chunk. Only the file's own share of the pool is
 * considered, so a package being streamed does not take slots the file is
 * not allowed to fill while others are empty. */
static inline struct sd_cache_line *sd_cache_victim( struct sd_cache_file *file,
                                                     const struct sd_cache_pool *pool )
{
    struct sd_cache_line *victim = &file->lines[0];
    unsigned int lines = sd_cache_file_lines( pool ), i, rank, best = 3;

    for (i = 0; i < lines; i++)
    {
        struct sd_cache_line *line = &file->lines[i];

        line->lru >>= 1;
        rank = !line->data ? 1 : line->start < 0 ? 0 : 2;
        if (rank < best || (rank == 2 && best == 2 && line->lru < victim->lru))
        {
            victim = line;
            best = rank;
        }
    }
    victim->lru = 0x80000000u;
    return victim;
}

/* Copy size bytes at offset into buf, filling chunks through fill. Returns the
 * bytes copied (fewer only at the end of the file), -1 when a request fails
 * before anything was copied, or SD_CACHE_BYPASS when no chunk can be
 * allocated. *fills counts the requests made. */
static inline long long sd_cache_read( struct sd_cache_file *file, struct sd_cache_pool *pool, long long offset,
                                       char *buf, size_t size, sd_cache_fill_fn fill, void *ctx,
                                       unsigned int *fills )
{
    size_t done = 0;

    while (done < size)
    {
        long long at = offset + (long long)done;
        struct sd_cache_line *line = sd_cache_line_for( file, at );
        size_t skip, count;

        if (!line)
        {
            long long start, got;
            unsigned int window;
            size_t want;

            if (!sd_cache_plan( file, pool, at, &start, &want, &window )) break;
            line = sd_cache_victim( file, pool );
            if (!line->data && pool->used >= pool->max)
            {
                /* Full: take the chunk read longest ago, whoever read it. */
                struct sd_cache_line *old = sd_cache_oldest( pool );

                if (!old) return done ? (long long)done : SD_CACHE_BYPASS;
                line->data = old->data;
                old->data = NULL;
                old->start = -1;
                old->len = 0;
                old->lru = 0;
                sd_cache_held_replace( pool, old, line );
            }
            else if (!line->data)
            {
                if (!pool->held &&
                    !(pool->held = calloc( pool->cap ? pool->cap : pool->max, sizeof(*pool->held) )))
                    return done ? (long long)done : SD_CACHE_BYPASS;
                if (!(line->data = malloc( SD_CACHE_CHUNK )))
                    return done ? (long long)done : SD_CACHE_BYPASS;
                sd_cache_held_add( pool, line );
            }
            line->start = -1;
            line->len = 0;
            (*fills)++;
            if ((got = fill( ctx, start, line->data, want )) < 0)
                return done ? (long long)done : -1;
            line->start = start;
            line->len = (size_t)got;
            file->window = window;
            file->fill_end = start + got;
            if ((size_t)got < want)  /* the end of the file */
            {
                file->size = start + got;
                file->size_known = 1;
            }
            if (at >= start + got) break;
        }
        line->used_at = ++pool->clock;
        skip = (size_t)(at - line->start);
        count = line->len - skip;
        if (count > size - done) count = size - done;
        memcpy( buf + done, line->data + skip, count );
        done += count;
    }
    return (long long)done;
}

/* FAT ignores ASCII case, repeated and trailing slashes name the same file,
 * and a path without a device is on sdmc, the default device. */
static inline int sd_cache_same_path( const char *a, const char *b )
{
    if (!strncmp( a, "sdmc:", 5 )) a += 5;
    if (!strncmp( b, "sdmc:", 5 )) b += 5;
    for (;;)
    {
        char ca, cb;

        while (a[0] == '/' && (a[1] == '/' || !a[1])) a++;
        while (b[0] == '/' && (b[1] == '/' || !b[1])) b++;
        ca = *a;
        cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
        if (!ca) return 1;
        a++;
        b++;
    }
}

/* Whether `path` names `dir` or a file below it, compared the way the card
 * compares names (case, separators, and a trailing separator that names the
 * same directory), with the boundary where the directory's name ends: "/a/bc"
 * is not under "/a/b", while "/a/b/c" and "/a/b" are. A directory that is
 * nothing but separators is the root, and everything is under it. */
static inline int sd_cache_path_under( const char *path, const char *dir )
{
    size_t length, i;

    if (!path || !dir) return 0;
    if (!strncmp( path, "sdmc:", 5 )) path += 5;
    if (!strncmp( dir, "sdmc:", 5 )) dir += 5;
    length = strlen( dir );
    while (length && (dir[length - 1] == '/' || dir[length - 1] == '\\')) length--;
    for (i = 0; i < length; i++)
    {
        char a = path[i], b = dir[i];

        if (!a) return 0;
        if (a == '\\') a = '/';
        if (b == '\\') b = '/';
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return 0;
    }
    return !path[i] || path[i] == '/' || path[i] == '\\';
}

static inline struct sd_cache_file *sd_cache_find( struct sd_cache_file *list, void *key )
{
    for (; list; list = list->next) if (list->key == key) return list;
    return NULL;
}

/* Stop caching every open file with this path. */
static inline void sd_cache_forget_path( struct sd_cache_file *list, struct sd_cache_pool *pool, const char *path )
{
    for (; list; list = list->next)
    {
        if (!sd_cache_same_path( list->path, path )) continue;
        list->cacheable = 0;
        sd_cache_drop( list, pool );
    }
}

/* Bytes went to this path: what every open file with it holds may be out of
 * date, and is dropped. They stay cached, and read the card again. */
static inline void sd_cache_written( struct sd_cache_file *list, struct sd_cache_pool *pool, const char *path )
{
    for (; list; list = list->next)
        if (sd_cache_same_path( list->path, path )) sd_cache_drop( list, pool );
}

/* Record an open file. Returns NULL without memory. */
static inline struct sd_cache_file *sd_cache_opened( struct sd_cache_file **list, struct sd_cache_pool *pool,
                                                     void *key, const char *path, int writable )
{
    struct sd_cache_file *file, *other;

    if (!(file = calloc( 1, sizeof(*file) ))) return NULL;
    if (!(file->path = strdup( path )))
    {
        free( file );
        return NULL;
    }
    file->key = key;
    file->writable = writable;
    file->cacheable = 1;
    /* What a writer of this path left behind is not what this open will read. */
    for (other = *list; other; other = other->next)
        if (other->writable && sd_cache_same_path( other->path, path )) sd_cache_drop( other, pool );
    file->next = *list;
    *list = file;
    return file;
}

static inline void sd_cache_closed( struct sd_cache_file **list, struct sd_cache_pool *pool, void *key )
{
    struct sd_cache_file **prev, *file;

    for (prev = list; (file = *prev); prev = &file->next)
    {
        if (file->key != key) continue;
        *prev = file->next;
        sd_cache_drop( file, pool );
        free( file->path );
        free( file );
        return;
    }
}

#endif
