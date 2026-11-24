/* Stutter: the time between frames and what took it (frame_pacing.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

#include "carafe_fex_stats.h"
#include "frame_pacing.h"

/* Frames remembered for the percentiles; a report every 10 seconds sees 600
 * at 60 frames a second. Those past it count in the totals only. */
#define FRAMES_KEPT 4096
#define FRAMES_SLOWEST 3
/* A gap this long is a loading screen or a menu, not a stutter. */
#define FRAME_GAP_US 1000000u

/* sd_cache.c, and the GPU's shader compiler (shader_compile.c) where the
 * runtime has one. */
extern unsigned long long wine_nx_sd_read_ns __attribute__((weak));
extern unsigned long long wine_nx_shader_compile_ns __attribute__((weak));
extern unsigned int wine_nx_shader_compiles __attribute__((weak));

struct frame_cost
{
    unsigned int us;          /* since the frame before */
    unsigned int fex_us;      /* FEX translating code on the presenting thread */
    unsigned int fex_all_us;  /* on every thread */
    unsigned int card_us;     /* the card reading, any thread */
    unsigned int shader_us;   /* the shader compiler, any thread */
    unsigned int shaders;     /* shaders it compiled */
    unsigned int present_us;  /* the present itself */
};

struct frame_counters
{
    u64 tick;
    unsigned int tid;
    u64 fex, fex_all;
    unsigned long long card_ns, shader_ns;
    unsigned int shaders;
};

static Mutex frames_lock;
static struct frame_counters last;
static int have_last, have_fex;
static unsigned int frames, gaps, kept;
static unsigned long long frames_us;
static unsigned int frame_us[FRAMES_KEPT];
static struct frame_cost slowest[FRAMES_SLOWEST];
static unsigned int slowest_count;

static unsigned int ticks_us( u64 ticks )
{
    u64 us = armTicksToNs( ticks ) / 1000;

    return us > 0xffffffffull ? 0xffffffffu : (unsigned int)us;
}

static unsigned int ns_us( unsigned long long now, unsigned long long before )
{
    return now > before ? (unsigned int)((now - before) / 1000) : 0;
}

static void keep_slowest( const struct frame_cost *cost )
{
    unsigned int at = slowest_count < FRAMES_SLOWEST ? slowest_count++ : FRAMES_SLOWEST;

    while (at > 0 && slowest[at - 1].us < cost->us)
    {
        if (at < FRAMES_SLOWEST) slowest[at] = slowest[at - 1];
        at--;
    }
    if (at < FRAMES_SLOWEST) slowest[at] = *cost;
}

void wine_nx_frame_presented( unsigned int tid, unsigned long long present_100ns )
{
    struct frame_counters now = { .tick = armGetSystemTick(), .tid = tid };
    int fex = carafeFexStatsJitTicks( tid, &now.fex, &now.fex_all );

    if (&wine_nx_sd_read_ns) now.card_ns = __atomic_load_n( &wine_nx_sd_read_ns, __ATOMIC_RELAXED );
    if (&wine_nx_shader_compile_ns)
    {
        now.shader_ns = __atomic_load_n( &wine_nx_shader_compile_ns, __ATOMIC_RELAXED );
        now.shaders = __atomic_load_n( &wine_nx_shader_compiles, __ATOMIC_RELAXED );
    }
    mutexLock( &frames_lock );
    if (have_last && now.tick > last.tick)
    {
        struct frame_cost cost = { .us = ticks_us( now.tick - last.tick ) };

        if (cost.us >= FRAME_GAP_US) gaps++;
        else
        {
            if (fex)
            {
                /* Another thread presented the frame before: its count says
                 * nothing of this one's. */
                if (last.tid == tid && now.fex > last.fex) cost.fex_us = ticks_us( now.fex - last.fex );
                if (now.fex_all > last.fex_all) cost.fex_all_us = ticks_us( now.fex_all - last.fex_all );
                have_fex = 1;
            }
            cost.card_us = ns_us( now.card_ns, last.card_ns );
            cost.shader_us = ns_us( now.shader_ns, last.shader_ns );
            cost.shaders = now.shaders - last.shaders;
            cost.present_us = (unsigned int)(present_100ns / 10);
            frames++;
            frames_us += cost.us;
            if (kept < FRAMES_KEPT) frame_us[kept++] = cost.us;
            keep_slowest( &cost );
        }
    }
    last = now;
    have_last = 1;
    mutexUnlock( &frames_lock );
}

static int by_time( const void *a, const void *b )
{
    unsigned int x = *(const unsigned int *)a, y = *(const unsigned int *)b;

    return x < y ? -1 : x > y;
}

/* As 12.3 ms. */
static int put_ms( char *line, size_t size, unsigned int us )
{
    return snprintf( line, size, "%u.%u ms", us / 1000, us % 1000 / 100 );
}

int wine_nx_frame_report( char *line, size_t size )
{
    static unsigned int sorted[FRAMES_KEPT];
    struct frame_cost worst[FRAMES_SLOWEST];
    unsigned int count, gap_count, stored, worst_count, median, high, stutters = 0, long_frames = 0, i;
    unsigned long long total_us;
    int fex, used;

    mutexLock( &frames_lock );
    count = frames;
    gap_count = gaps;
    total_us = frames_us;
    stored = kept;
    worst_count = slowest_count;
    fex = have_fex;
    memcpy( sorted, frame_us, stored * sizeof(*sorted) );
    memcpy( worst, slowest, sizeof(worst) );
    frames = gaps = kept = slowest_count = have_fex = 0;
    frames_us = 0;
    mutexUnlock( &frames_lock );

    if (!count && !gap_count) return 0;
    if (!count)
        return snprintf( line, size, "[FRAMES] no frames, %u gaps of a second or more", gap_count );
    qsort( sorted, stored, sizeof(*sorted), by_time );
    median = sorted[stored / 2];
    high = sorted[stored - 1 - stored / 100];
    for (i = 0; i < stored; i++)
    {
        /* Twice the median, and at least 8 ms more: a frame that waited a
         * whole refresh longer than its neighbours. */
        if (sorted[i] > 2 * median && sorted[i] >= median + 8000) stutters++;
        if (sorted[i] >= 100000) long_frames++;
    }
    used = snprintf( line, size, "[FRAMES] %u in %llu.%llu s: median ", count,
                     total_us / 1000000, total_us % 1000000 / 100000 );
#define PUT(...) do { if (used > 0 && (size_t)used < size) used += snprintf( line + used, size - (size_t)used, __VA_ARGS__ ); } while (0)
#define PUT_MS(us) do { if (used > 0 && (size_t)used < size) used += put_ms( line + used, size - (size_t)used, (us) ); } while (0)
    PUT_MS( median );
    PUT( ", 99th percentile " );
    PUT_MS( high );
    PUT( ", over twice the median %u, over 100 ms %u", stutters, long_frames );
    if (gap_count) PUT( ", gaps of a second or more %u", gap_count );
    PUT( "; slowest" );
    for (i = 0; i < worst_count; i++)
    {
        const struct frame_cost *cost = &worst[i];

        PUT( "%s ", i ? ";" : "" );
        PUT_MS( cost->us );
        PUT( ":" );
        if (fex) PUT( " FEX translating %u ms (%u ms on all threads),", cost->fex_us / 1000, cost->fex_all_us / 1000 );
        PUT( " card %u ms,", cost->card_us / 1000 );
        if (&wine_nx_shader_compile_ns) PUT( " %u shaders compiled in %u ms,", cost->shaders, cost->shader_us / 1000 );
        PUT( " present %u ms", cost->present_us / 1000 );
    }
#undef PUT_MS
#undef PUT
    return used;
}
