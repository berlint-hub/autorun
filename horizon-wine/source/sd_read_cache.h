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
#define SD_CACHE_LINES  8             /* chunks kept per file */
#define SD_CACHE_DIRECT (64 * 1024)   /* reads this large go straight to the file */
#define SD_CACHE_BYPASS (-2)          /* sd_cache_read found no memory: read directly */
#define SD_CACHE_POOL_MIN 256         /* 32 MB, the chunks every game gets */
#define SD_CACHE_POOL_MAX 1536        /* 192 MB, when a game leaves that much heap free */
#define SD_CACHE_WHOLE_CHUNKS 2048    /* 256 MB of a file that keeps every chunk (keep_whole) */

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
/* A chunk of a file read and closed, kept for the next open of its path
 * (sd_cache_shelve): its line is in the pool's held list like any other, and
 * the slot is free while line.data is NULL. */
struct sd_cache_shelved
{
    struct sd_cache_line line;
    char *path;
};

struct sd_cache_pool
{
    unsigned int used, max;        /* chunks allocated for all files, and the limit */
    unsigned int cap;              /* how many held can list; max never passes it */
    unsigned int fill_min;         /* the smallest fill, or 0 for whole chunks */
    unsigned long long clock;      /* counts every chunk read, across files */
    struct sd_cache_line **held;   /* the lines holding a chunk, used of them */
    struct sd_cache_shelved *shelf;  /* shelf_size slots, allocated with the first */
    unsigned int shelf_size;
    int no_shelf;                  /* a writer could not be followed: nothing is kept */
};

/* With no chunk held anywhere, no slot holds one either. */
static inline void sd_cache_shelf_free( struct sd_cache_pool *pool )
{
    unsigned int i;

    for (i = 0; i < pool->shelf_size; i++) free( pool->shelf[i].path );
    free( pool->shelf );
    pool->shelf = NULL;
    pool->shelf_size = 0;
}

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
        sd_cache_shelf_free( pool );
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
    /* For the [SDFILES] line (sd_cache.c): what the program asked of this
     * open and what the card gave for it, and how much was reported. */
    unsigned long long asked, card, asked_told, card_told;
    struct sd_cache_line lines[SD_CACHE_LINES];
    /* FEX's code cache (sd_cache_keeps_whole): read a few kilobytes at a time
     * all over, while FEX adds what it translates at the end. Eight chunks
     * were far too few for it, and every addition threw the eight away, so
     * each block found in it cost a 128 KB read from the card, 0.4 to 7 ms,
     * more than translating the block again. Such a file keeps every chunk it
     * reads, chunk i of the file in whole[i] (allocated with the first), for
     * as long as the pool has room: they go, oldest first, like any other
     * file's. A write puts its bytes into the chunks it lands in
     * (sd_cache_wrote). Past SD_CACHE_WHOLE_CHUNKS the eight lines serve. */
    int keep_whole;
    struct sd_cache_line *whole;
    struct sd_cache_file *next;
};

/* Reads size bytes at offset into buf. Returns the bytes read or -1. */
typedef long long (*sd_cache_fill_fn)( void *ctx, long long offset, char *buf, size_t size );

static inline void sd_cache_line_free( struct sd_cache_line *line, struct sd_cache_pool *pool )
{
    if (!line->data) return;
    free( line->data );
    sd_cache_held_remove( pool, line );
    memset( line, 0, sizeof(*line) );
}

static inline void sd_cache_drop( struct sd_cache_file *file, struct sd_cache_pool *pool )
{
    unsigned int i;

    for (i = 0; i < SD_CACHE_LINES; i++) sd_cache_line_free( &file->lines[i], pool );
    if (file->whole)
    {
        for (i = 0; i < SD_CACHE_WHOLE_CHUNKS; i++) sd_cache_line_free( &file->whole[i], pool );
        free( file->whole );
        file->whole = NULL;
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

/* The line to fill: a buffer holding nothing, then a free slot, then the
 * least recently used chunk. */
static inline struct sd_cache_line *sd_cache_victim( struct sd_cache_file *file )
{
    struct sd_cache_line *victim = &file->lines[0];
    unsigned int i, rank, best = 3;

    for (i = 0; i < SD_CACHE_LINES; i++)
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

/* Whether path a is b, or lies under the directory b; any path when b is
 * NULL. */
static inline int sd_cache_under_path( const char *a, const char *b )
{
    if (!b) return 1;
    if (!strncmp( a, "sdmc:", 5 )) a += 5;
    if (!strncmp( b, "sdmc:", 5 )) b += 5;
    for (;;)
    {
        char ca, cb;

        while (a[0] == '/' && (a[1] == '/' || !a[1])) a++;
        while (b[0] == '/' && (b[1] == '/' || !b[1])) b++;
        ca = *a;
        cb = *b;
        if (!cb) return !ca || ca == '/';
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
        a++;
        b++;
    }
}

/* The shelved chunk of path that holds offset. */
static inline struct sd_cache_shelved *sd_cache_shelf_find( struct sd_cache_pool *pool, const char *path,
                                                            long long offset )
{
    unsigned int i;

    for (i = 0; i < pool->shelf_size; i++)
    {
        struct sd_cache_shelved *slot = &pool->shelf[i];

        if (slot->line.data && slot->line.start >= 0 && offset >= slot->line.start &&
            offset < slot->line.start + (long long)slot->line.len && sd_cache_same_path( slot->path, path ))
            return slot;
    }
    return NULL;
}

/* What is shelved of path, or of anything under the directory path, or of
 * everything (NULL), is out of date: written, truncated, renamed, deleted. */
static inline void sd_cache_shelf_forget( struct sd_cache_pool *pool, const char *path )
{
    unsigned int i;

    for (i = 0; i < pool->shelf_size; i++)
    {
        struct sd_cache_shelved *slot = &pool->shelf[i];

        if (!slot->path || !sd_cache_under_path( slot->path, path )) continue;
        free( slot->path );
        slot->path = NULL;
        if (!slot->line.data) continue;
        free( slot->line.data );
        memset( &slot->line, 0, sizeof(slot->line) );
        slot->line.start = -1;
        sd_cache_held_remove( pool, &slot->line );
        if (!pool->shelf) return;  /* that was the last chunk held, and the shelf went with it */
    }
}

/* A file read and closed leaves its chunks for the next open of its path,
 * in the pool with every other chunk until it needs them. Doom 3 opens its
 * pk4 archive again for every file it reads from it (unzReOpen), and each
 * open started with nothing: the card gave 1.9 GB of the archives for the
 * 0.3 GB the game asked for while a level loaded. */
static inline void sd_cache_shelve( struct sd_cache_file *file, struct sd_cache_pool *pool )
{
    unsigned int i, j, size = pool->cap ? pool->cap : pool->max;

    if (pool->no_shelf || !file->path) return;
    if (!pool->shelf)
    {
        if (!size || !(pool->shelf = calloc( size, sizeof(*pool->shelf) ))) return;
        pool->shelf_size = size;
    }
    for (i = 0; i < SD_CACHE_LINES; i++)
    {
        struct sd_cache_line *line = &file->lines[i];
        struct sd_cache_shelved *slot = NULL;

        if (!line->data || line->start < 0) continue;
        for (j = 0; j < pool->shelf_size; j++)
        {
            struct sd_cache_shelved *other = &pool->shelf[j];

            if (!other->line.data)
            {
                if (!slot) slot = other;
                continue;
            }
            /* Two opens read the same chunk: one copy is enough. */
            if (other->line.start == line->start && sd_cache_same_path( other->path, file->path ))
            {
                if (other->line.used_at < line->used_at) other->line.used_at = line->used_at;
                slot = NULL;
                break;
            }
        }
        if (!slot) continue;
        if (!slot->path || !sd_cache_same_path( slot->path, file->path ))
        {
            char *path = strdup( file->path );

            if (!path) continue;
            free( slot->path );
            slot->path = path;
        }
        slot->line = *line;
        sd_cache_held_replace( pool, line, &slot->line );
        memset( line, 0, sizeof(*line) );
        line->start = -1;
    }
}

/* Gives a line with no buffer a chunk's: a new one, or with the pool full the
 * one read longest ago, whoever read it. 0 without memory. */
static inline int sd_cache_take_chunk( struct sd_cache_pool *pool, struct sd_cache_line *line )
{
    if (line->data) return 1;
    if (pool->used >= pool->max)
    {
        /* Full: take the chunk read longest ago, whoever read it. */
        struct sd_cache_line *old = sd_cache_oldest( pool );

        if (!old) return 0;
        line->data = old->data;
        old->data = NULL;
        old->start = -1;
        old->len = 0;
        old->lru = 0;
        sd_cache_held_replace( pool, old, line );
        return 1;
    }
    if (!pool->held && !(pool->held = calloc( pool->cap ? pool->cap : pool->max, sizeof(*pool->held) )))
        return 0;
    if (!(line->data = malloc( SD_CACHE_CHUNK ))) return 0;
    sd_cache_held_add( pool, line );
    return 1;
}

/* The line for the chunk at offset of a file that keeps every chunk, or NULL
 * when the file does not or the offset is past what it can keep. */
static inline struct sd_cache_line *sd_cache_whole_line( struct sd_cache_file *file, long long offset )
{
    long long index = offset / SD_CACHE_CHUNK;

    if (!file->keep_whole || offset < 0 || index >= SD_CACHE_WHOLE_CHUNKS) return NULL;
    if (!file->whole && !(file->whole = calloc( SD_CACHE_WHOLE_CHUNKS, sizeof(*file->whole) ))) return NULL;
    return &file->whole[index];
}

/* Bytes written at offset through this open. A file that keeps every chunk
 * takes them into the chunks it holds, as the card has them once they reach
 * it; a chunk holding less than a whole one that the bytes run past the end of
 * is let go, to be read again. Any other file, or a write at a place not
 * known, drops everything. The caller calls it again once the bytes reached
 * the card, for a reader that refilled a chunk while they were on the way. */
static inline void sd_cache_line_wrote( struct sd_cache_line *line, struct sd_cache_pool *pool, long long offset,
                                        const char *data, size_t size )
{
    long long end = offset + (long long)size, from, to;

    if (!line->data || line->start < 0) return;
    if (end <= line->start || offset >= line->start + SD_CACHE_CHUNK) return;
    if (line->len < SD_CACHE_CHUNK && end > line->start + (long long)line->len)
    {
        sd_cache_line_free( line, pool );
        return;
    }
    from = offset > line->start ? offset : line->start;
    to = end < line->start + (long long)line->len ? end : line->start + (long long)line->len;
    if (from < to) memcpy( line->data + (from - line->start), data + (from - offset), (size_t)(to - from) );
}

static inline void sd_cache_wrote( struct sd_cache_file *file, struct sd_cache_pool *pool, long long offset,
                                   const char *data, size_t size )
{
    long long index, last;
    unsigned int i;

    if (!file->keep_whole || offset < 0)
    {
        sd_cache_drop( file, pool );
        return;
    }
    for (i = 0; i < SD_CACHE_LINES; i++) sd_cache_line_wrote( &file->lines[i], pool, offset, data, size );
    if (file->whole && size)
    {
        last = (offset + (long long)size - 1) / SD_CACHE_CHUNK;
        for (index = offset / SD_CACHE_CHUNK; index <= last && index < SD_CACHE_WHOLE_CHUNKS; index++)
            sd_cache_line_wrote( &file->whole[index], pool, offset, data, size );
    }
    if (file->size_known && offset + (long long)size > file->size) file->size_known = 0;
    file->window = 0;
    file->fill_end = 0;
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
        struct sd_cache_line *line = sd_cache_whole_line( file, at );
        size_t skip, count;

        struct sd_cache_shelved *shelved;

        if (line)
        {
            if (!line->data || line->start < 0 || at >= line->start + (long long)line->len)
            {
                /* Not read yet, taken by another file, or the file has grown
                 * past it: chunk i is read again, whole. */
                long long start = at - at % SD_CACHE_CHUNK, got;
                size_t want = SD_CACHE_CHUNK;

                if (file->size_known && at >= file->size) break;
                if (file->size_known && start + (long long)want > file->size) want = (size_t)(file->size - start);
                if (!sd_cache_take_chunk( pool, line )) return done ? (long long)done : SD_CACHE_BYPASS;
                line->start = -1;
                line->len = 0;
                (*fills)++;
                if ((got = fill( ctx, start, line->data, want )) < 0)
                    return done ? (long long)done : -1;
                line->start = start;
                line->len = (size_t)got;
                if ((size_t)got < want)  /* the end of the file */
                {
                    file->size = start + got;
                    file->size_known = 1;
                }
                if (at >= start + got) break;
            }
            line->lru = 0x80000000u;
        }
        else if ((line = sd_cache_line_for( file, at ))) {}
        else if (file->path && pool->shelf && (shelved = sd_cache_shelf_find( pool, file->path, at )))
        {
            /* An earlier open of the path read it: the chunk comes off the
             * shelf, and the one it replaces here goes there in its place. */
            line = sd_cache_victim( file );
            if (line->data && line->start >= 0)
            {
                struct sd_cache_line kept = *line;

                *line = shelved->line;
                shelved->line = kept;
            }
            else
            {
                if (line->data)  /* a buffer holding nothing */
                {
                    free( line->data );
                    line->data = NULL;
                    sd_cache_held_remove( pool, line );
                }
                *line = shelved->line;
                sd_cache_held_replace( pool, &shelved->line, line );
                memset( &shelved->line, 0, sizeof(shelved->line) );
                shelved->line.start = -1;
            }
            line->lru = 0x80000000u;
        }
        else if (!line)
        {
            long long start, got;
            unsigned int window;
            size_t want;

            if (!sd_cache_plan( file, pool, at, &start, &want, &window )) break;
            line = sd_cache_victim( file );
            if (!sd_cache_take_chunk( pool, line )) return done ? (long long)done : SD_CACHE_BYPASS;
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

static inline struct sd_cache_file *sd_cache_find( struct sd_cache_file *list, void *key )
{
    for (; list; list = list->next) if (list->key == key) return list;
    return NULL;
}

/* Stop caching every open file with this path. */
static inline void sd_cache_forget_path( struct sd_cache_file *list, struct sd_cache_pool *pool, const char *path )
{
    sd_cache_shelf_forget( pool, path );
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
    sd_cache_shelf_forget( pool, path );
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
    /* A writer's bytes may reach the card from it alone, without a reader
     * to see them: what was shelved of the path is not kept past it. */
    if (writable) sd_cache_shelf_forget( pool, path );
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
        if (file->cacheable && !file->writable) sd_cache_shelve( file, pool );
        sd_cache_drop( file, pool );
        free( file->path );
        free( file );
        return;
    }
}

#endif
