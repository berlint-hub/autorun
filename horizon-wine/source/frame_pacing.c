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
/* With Mesa's GL thread (carafe_mesa.c, carafe_diag.c): the time the program's
 * thread spent handing it calls or waiting for it, and what the GL thread and
 * the driver's thread behind it ran. Faults handed to the program (horizon.c). */
extern unsigned long long wine_nx_gl_wait_ns __attribute__((weak));
extern unsigned long long carafe_native_thread_ticks( const char *name ) __attribute__((weak));
extern unsigned int wine_nx_program_faults __attribute__((weak));
/* The profiler's samples within a slow frame, of every thread it samples
 * (thread_profile.c), and what each is part of. */
extern unsigned int wine_nx_prof_frame_samples( unsigned long long from, unsigned long long to, unsigned int *tids,
                                                unsigned char *kinds, unsigned long long *keys,
                                                unsigned long long *chains, unsigned int max ) __attribute__((weak));
extern void wine_nx_prof_sample_group( unsigned int kind, unsigned long long key, unsigned long long chain,
                                       char *buf, size_t size ) __attribute__((weak));

/* A round of samples every 2 ms over at most four threads: 160 cover a frame
 * of 80 ms of all four, the newest part of a longer one. */
#define FRAME_SAMPLES 160
#define FRAME_SAMPLE_US 2000
#define FRAME_GROUPS_SHOWN 3

struct frame_cost
{
    unsigned int us;          /* since the frame before */
    unsigned int fex_us;      /* FEX translating code on the presenting thread */
    unsigned int fex_all_us;  /* on every thread */
    unsigned int card_us;     /* the card reading, any thread */
    unsigned int shader_us;   /* the shader compiler, any thread */
    unsigned int shaders;     /* shaders it compiled */
    unsigned int present_us;  /* the present itself */
    unsigned int gl_wait_us;  /* handing calls to the GL thread, or waiting for it */
    unsigned int gl_us;       /* the GL thread running */
    unsigned int driver_us;   /* the driver's thread behind it running */
    unsigned int faults;      /* faults handed to the program */
    unsigned int tid;         /* the thread that presented */
    unsigned int samples;     /* the profiler's, of every thread, the newest first */
    unsigned int sample_tids[FRAME_SAMPLES];
    unsigned char sample_kinds[FRAME_SAMPLES];
    unsigned long long sample_keys[FRAME_SAMPLES], sample_chains[FRAME_SAMPLES];
};

struct frame_counters
{
    u64 tick;
    unsigned int tid;
    u64 fex, fex_all;
    unsigned long long card_ns, shader_ns, gl_wait_ns;
    unsigned int shaders, faults;
    u64 gl, driver;
};

static Mutex frames_lock;
static struct frame_counters last;
static int have_last, have_fex, have_gl;
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
    if (&wine_nx_gl_wait_ns) now.gl_wait_ns = __atomic_load_n( &wine_nx_gl_wait_ns, __ATOMIC_RELAXED );
    if (carafe_native_thread_ticks)
    {
        now.gl = carafe_native_thread_ticks( "gl" );
        now.driver = carafe_native_thread_ticks( "gdrv" );
    }
    if (&wine_nx_program_faults) now.faults = __atomic_load_n( &wine_nx_program_faults, __ATOMIC_RELAXED );
    mutexLock( &frames_lock );
    if (have_last && now.tick > last.tick)
    {
        struct frame_cost cost = { .us = ticks_us( now.tick - last.tick ), .tid = tid };

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
            cost.gl_wait_us = ns_us( now.gl_wait_ns, last.gl_wait_ns );
            /* A thread that ended takes its time out of the sum. */
            if (now.gl > last.gl) cost.gl_us = ticks_us( now.gl - last.gl );
            if (now.driver > last.driver) cost.driver_us = ticks_us( now.driver - last.driver );
            cost.faults = now.faults - last.faults;
            /* Where the threads were, for a frame among the slowest. */
            if (wine_nx_prof_frame_samples && last.tid == tid &&
                (slowest_count < FRAMES_SLOWEST || cost.us > slowest[FRAMES_SLOWEST - 1].us))
                cost.samples = wine_nx_prof_frame_samples( last.tick, now.tick, cost.sample_tids, cost.sample_kinds,
                                                           cost.sample_keys, cost.sample_chains, FRAME_SAMPLES );
            /* Only with a GL thread: without one Mesa's finish returns at once. */
            if (now.gl) have_gl = 1;
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

/* The places thread tid was sampled in a slow frame, added up by what they
 * are part of (a module of the game, a function of the runtime, a system call
 * and what waits in it), the three it was in most, as the time each stands
 * for. A round samples each thread once, so with all of the frame's samples
 * at hand each stands for the frame's time over their number; past
 * FRAME_SAMPLES only the last are, at the profiler's period. */
static int put_samples( char *line, size_t size, const struct frame_cost *cost, unsigned int tid, const char *who )
{
    struct { char name[96]; unsigned int count; } groups[16], swap;
    unsigned int count = 0, total = 0, i, j;
    unsigned long long per_us;
    int used;

    for (i = 0; i < cost->samples; i++)
    {
        char name[96];

        if (cost->sample_tids[i] != tid) continue;
        total++;
        wine_nx_prof_sample_group( cost->sample_kinds[i], cost->sample_keys[i], cost->sample_chains[i], name,
                                   sizeof(name) );
        for (j = 0; j < count && strcmp( groups[j].name, name ); j++) continue;
        if (j == count)
        {
            if (count == sizeof(groups) / sizeof(groups[0])) continue;
            snprintf( groups[count].name, sizeof(groups[count].name), "%s", name );
            groups[count++].count = 0;
        }
        groups[j].count++;
    }
    if (!total) return 0;
    per_us = cost->samples < FRAME_SAMPLES ? cost->us / total : FRAME_SAMPLE_US;
    for (i = 1; i < count; i++)
        for (j = i; j > 0 && groups[j - 1].count < groups[j].count; j--)
        {
            swap = groups[j - 1];
            groups[j - 1] = groups[j];
            groups[j] = swap;
        }
    used = snprintf( line, size, " %s in", who );
    for (i = 0; i < count && i < FRAME_GROUPS_SHOWN && used > 0 && (size_t)used < size; i++)
        used += snprintf( line + used, size - (size_t)used, "%s %s %u ms", i ? "," : "", groups[i].name,
                          (unsigned int)(groups[i].count * per_us / 1000) );
    if (used > 0 && (size_t)used < size)
        used += snprintf( line + used, size - (size_t)used, " (%s%u samples),",
                          cost->samples < FRAME_SAMPLES ? "" : "the last ", total );
    return used;
}

/* The thread other than the presenting one sampled most in the frame: the
 * one it may have waited for. 0 for none. */
static unsigned int busiest_other( const struct frame_cost *cost )
{
    unsigned int best = 0, best_count = 0, i, j, count;

    for (i = 0; i < cost->samples; i++)
    {
        unsigned int tid = cost->sample_tids[i];

        if (tid == cost->tid || tid == best) continue;
        for (j = count = 0; j < cost->samples; j++) count += cost->sample_tids[j] == tid;
        if (count > best_count)
        {
            best = tid;
            best_count = count;
        }
    }
    return best;
}

/* As 12.3 ms. */
static int put_ms( char *line, size_t size, unsigned int us )
{
    return snprintf( line, size, "%u.%u ms", us / 1000, us % 1000 / 100 );
}

int wine_nx_frame_report( char *line, size_t size )
{
    static unsigned int sorted[FRAMES_KEPT];
    /* Only the thread report calls it. */
    static struct frame_cost worst[FRAMES_SLOWEST];
    unsigned int count, gap_count, stored, worst_count, median, high, stutters = 0, long_frames = 0, i;
    unsigned long long total_us;
    int fex, gl, used;

    mutexLock( &frames_lock );
    count = frames;
    gap_count = gaps;
    total_us = frames_us;
    stored = kept;
    worst_count = slowest_count;
    fex = have_fex;
    gl = have_gl;
    memcpy( sorted, frame_us, stored * sizeof(*sorted) );
    memcpy( worst, slowest, sizeof(worst) );
    frames = gaps = kept = slowest_count = have_fex = have_gl = 0;
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
    /* Each slow frame on a line of its own: a log line holds 1024 bytes. */
    for (i = 0; i < worst_count; i++)
    {
        const struct frame_cost *cost = &worst[i];

        PUT( "\n[FRAMES] slow frame " );
        PUT_MS( cost->us );
        PUT( ":" );
        if (fex) PUT( " FEX translating %u ms (%u ms on all threads),", cost->fex_us / 1000, cost->fex_all_us / 1000 );
        PUT( " card %u ms,", cost->card_us / 1000 );
        if (&wine_nx_shader_compile_ns) PUT( " %u shaders compiled in %u ms,", cost->shaders, cost->shader_us / 1000 );
        if (gl)
            PUT( " waiting for the GL thread %u ms (it ran %u ms, the driver's thread %u ms),", cost->gl_wait_us / 1000,
                 cost->gl_us / 1000, cost->driver_us / 1000 );
        if (cost->faults) PUT( " %u faults to the program,", cost->faults );
        if (cost->samples && wine_nx_prof_sample_group)
        {
            unsigned int other = busiest_other( cost );
            char who[24];

            if (used > 0 && (size_t)used < size) used += put_samples( line + used, size - (size_t)used, cost, cost->tid, "its thread" );
            snprintf( who, sizeof(who), "thread %u", other );
            if (other && used > 0 && (size_t)used < size) used += put_samples( line + used, size - (size_t)used, cost, other, who );
        }
        PUT( " present %u ms", cost->present_us / 1000 );
    }
#undef PUT_MS
#undef PUT
    return used;
}
