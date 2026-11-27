/* The [FRAMES] line on the host (source/frame_pacing.c): how evenly frames
 * came and what took the time in the slowest of them.
 *
 *   cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
 *      -I tests/carafe-shims -I source tests/frame_pacing.c -o /tmp/frame_pacing
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <switch.h>

u64 test_tick = 1000;
Handle test_thread = 0x51;
s32 test_priority = 59;
unsigned int test_priority_sets;

#include "carafe_fex_stats.c"
#include "../source/frame_pacing.c"  /* not this file, of the same name */

/* The runtime's weak symbols, defined after the code that asks whether they
 * are there. */
void wine_nx_runtime_trace( const char *msg ) { (void)msg; }
void *wine_nx_fex_stats_base;
u32 wine_nx_fex_stats_size;
unsigned long long wine_nx_sd_read_ns;
unsigned long long wine_nx_shader_compile_ns;
unsigned int wine_nx_shader_compiles;
unsigned long long wine_nx_gl_wait_ns;
unsigned int wine_nx_program_faults;
static unsigned long long gl_ticks, driver_ticks;

/* The profiler's samples: those of the next frame, for any thread. */
static unsigned int prof_count, prof_calls;
static unsigned int prof_tids[200];
static unsigned char prof_kinds[200];
static unsigned long long prof_keys[200], prof_chains[200];
unsigned int wine_nx_prof_frame_samples( unsigned long long from, unsigned long long to, unsigned int *tids,
                                         unsigned char *kinds, unsigned long long *keys,
                                         unsigned long long *chains, unsigned int max )
{
    unsigned int i;

    assert( from < to );
    prof_calls++;
    for (i = 0; i < prof_count && i < max; i++)
    {
        tids[i] = prof_tids[i];
        kinds[i] = prof_kinds[i];
        keys[i] = prof_keys[i];
        chains[i] = prof_chains[i];
    }
    return i;
}

void wine_nx_prof_sample_group( unsigned int kind, unsigned long long key, unsigned long long chain, char *buf,
                                size_t size )
{
    if (kind == 0) snprintf( buf, size, "%s", key < 0x1000000 ? "exe" : "gamex86.dll" );
    else if (chain) snprintf( buf, size, "svc%#llx in server_call_unlocked<NtWaitForSingleObject", key >> 40 );
    else snprintf( buf, size, "svc%#llx<condvarWaitTimeout", key >> 40 );
}

static void sample( unsigned int at, unsigned int tid, unsigned char kind, unsigned long long key,
                    unsigned long long chain )
{
    prof_tids[at] = tid;
    prof_kinds[at] = kind;
    prof_keys[at] = key;
    prof_chains[at] = chain;
}
/* The slow server calls of the next frame, as the profiler would give them. */
static unsigned int call_count, call_total, call_asks;
static struct wine_nx_slow_call calls_given[4];
unsigned int wine_nx_prof_frame_server_calls( unsigned long long from, unsigned long long to, unsigned int tid,
                                              struct wine_nx_slow_call *calls, unsigned int max,
                                              unsigned int *total )
{
    unsigned int i;

    assert( from < to );
    call_asks++;
    *total = 0;
    if (tid != 4) return 0;
    for (i = 0; i < call_count && i < max; i++) calls[i] = calls_given[i];
    *total = call_total;
    return i;
}

void wine_nx_prof_name_x86( unsigned int address, char *buf, size_t size )
{
    snprintf( buf, size, "exe+%#x", address - 0x400000 );
}

unsigned long long carafe_native_thread_ticks( const char *name )
{
    return !strcmp( name, "gl" ) ? gl_ticks : !strcmp( name, "gdrv" ) ? driver_ticks : 0;
}

#define TICKS_PER_MS 19200ull

/* FEX's area: the header, then the Windows threads 4 and 20. */
static struct
{
    FexStatsHeader header;
    FexThreadStats threads[2];
} fex_area;

static char line[8192];

static void frame( unsigned int tid, double ms, unsigned long long present_100ns )
{
    test_tick += (u64)(ms * TICKS_PER_MS);
    wine_nx_frame_presented( tid, present_100ns );
}

static int report( void )
{
    int length = wine_nx_frame_report( line, sizeof(line) );

    if (length) printf( "%s\n", line );
    return length;
}

static void contains( const char *text )
{
    if (strstr( line, text )) return;
    fprintf( stderr, "missing \"%s\" in: %s\n", text, line );
    assert( 0 );
}

static void without_fex( void )
{
    unsigned int i;

    /* Nothing shown yet, nor after the first frame: it has no frame before. */
    assert( !report() );
    frame( 4, 0, 0 );
    assert( !report() );

    /* 20 frames at 60 a second, then one of 120 ms in which the card read for
     * 30 ms and the shader compiler made two shaders in 45 ms. */
    for (i = 0; i < 20; i++) frame( 4, 16.7, 20000 );
    wine_nx_sd_read_ns += 30000000;
    wine_nx_shader_compile_ns += 45000000;
    wine_nx_shader_compiles += 2;
    frame( 4, 120, 30000 );
    /* One of 40 ms: twice the median and 8 ms more. One of 30 ms is not. */
    frame( 4, 40, 20000 );
    frame( 4, 30, 20000 );
    assert( report() > 0 );
    contains( "[FRAMES] 23 in 0.5 s: median 16.7 ms, 99th percentile 120.0 ms," );
    contains( "over twice the median 2, over 100 ms 1\n" );
    contains( "\n[FRAMES] slow frame 120.0 ms: card 30 ms, 2 shaders compiled in 45 ms, present 3 ms\n" );
    contains( "\n[FRAMES] slow frame 40.0 ms: card 0 ms, 0 shaders compiled in 0 ms, present 2 ms\n" );
    contains( "\n[FRAMES] slow frame 30.0 ms:" );
    assert( !strstr( line, "FEX" ) );
    assert( !strstr( line, "gaps" ) );

    /* Counting starts again. */
    assert( !report() );

    /* A loading screen: 5 s without a frame is a gap, not a slow frame. */
    frame( 4, 5000, 0 );
    assert( report() > 0 );
    contains( "[FRAMES] no frames, 1 gaps of a second or more" );
    frame( 4, 16.7, 0 );
    frame( 4, 2000, 0 );
    frame( 4, 16.7, 0 );
    assert( report() > 0 );
    contains( "[FRAMES] 2 in 0.0 s" );
    contains( "gaps of a second or more 1\n" );
    contains( "\n[FRAMES] slow frame 16.7 ms:" );
}

static void with_fex( void )
{
    unsigned int i;

    fex_area.header.thread_stats_size = sizeof(FexThreadStats);
    fex_area.header.head = offsetof( typeof(fex_area), threads[0] );
    fex_area.threads[0].tid = 4;
    fex_area.threads[0].next = offsetof( typeof(fex_area), threads[1] );
    fex_area.threads[1].tid = 20;
    wine_nx_fex_stats_base = &fex_area;
    wine_nx_fex_stats_size = sizeof(fex_area);

    frame( 4, 16.7, 0 );
    for (i = 0; i < 10; i++) frame( 4, 16.7, 0 );
    /* FEX translated for 50 ms on the presenting thread and 10 ms on another
     * one within a frame of 80 ms. */
    fex_area.threads[0].jit_time += 50 * TICKS_PER_MS;
    fex_area.threads[1].jit_time += 10 * TICKS_PER_MS;
    frame( 4, 80, 0 );
    /* Thread 20 presents the next: what thread 4 counted says nothing of it. */
    fex_area.threads[0].jit_time += 7 * TICKS_PER_MS;
    frame( 20, 60, 0 );
    assert( report() > 0 );
    contains( "slow frame 80.0 ms: FEX translating 50 ms (60 ms on all threads), card 0 ms" );
    contains( "slow frame 60.0 ms: FEX translating 0 ms (7 ms on all threads)," );

    /* A broken area is left alone. */
    fex_area.header.thread_stats_size = 1;
    frame( 4, 16.7, 0 );
    frame( 4, 16.7, 0 );
    assert( report() > 0 );
    assert( !strstr( line, "FEX" ) );
    wine_nx_fex_stats_base = NULL;
}

static void with_gl_thread( void )
{
    unsigned int i;

    /* No GL thread: nothing said of one, though Mesa's finish was called. */
    wine_nx_gl_wait_ns += 1000;
    frame( 4, 16.7, 0 );
    frame( 4, 16.7, 0 );
    assert( report() > 0 );
    assert( !strstr( line, "GL thread" ) && !strstr( line, "faults" ) );

    /* A door opens: a frame of 70 ms in which the program's thread waited
     * 50 ms for the GL thread, which ran 45 ms and its driver thread 30 ms,
     * and the program took 3 faults. */
    gl_ticks = driver_ticks = 1;
    frame( 4, 16.7, 0 );
    for (i = 0; i < 10; i++)
    {
        gl_ticks += 5 * TICKS_PER_MS;
        driver_ticks += 2 * TICKS_PER_MS;
        frame( 4, 16.7, 0 );
    }
    wine_nx_gl_wait_ns += 50000000;
    gl_ticks += 45 * TICKS_PER_MS;
    driver_ticks += 30 * TICKS_PER_MS;
    wine_nx_program_faults += 3;
    frame( 4, 70, 0 );
    assert( report() > 0 );
    contains( "slow frame 70.0 ms: card 0 ms, 0 shaders compiled in 0 ms, waiting for the GL thread 50 ms "
              "(it ran 45 ms, the driver's thread 30 ms), 3 faults to the program, present 0 ms\n" );
    contains( "slow frame 16.7 ms: card 0 ms, 0 shaders compiled in 0 ms, waiting for the GL thread 0 ms (it ran 5 ms, "
              "the driver's thread 2 ms), present 0 ms" );
    /* A GL thread that ended takes its time out of the sum: not a frame of
     * negative time. */
    gl_ticks = 1;
    frame( 4, 16.7, 0 );
    assert( report() > 0 );
    contains( "(it ran 0 ms," );
    gl_ticks = driver_ticks = 0;
}

static void with_samples( void )
{
    unsigned int i;

    /* A frame of 80 ms the profiler sampled 40 times on each of its threads:
     * the presenting one 24 times in the game's exe, 10 in its game DLL and 6
     * waiting for an object, the async thread (24) 30 times in the exe, and a
     * third (12) 5 times. */
    for (i = 0; i < 40; i++)
        sample( i, 4, i < 34 ? 0 : 3, i < 24 ? 0x4a0000 : i < 34 ? 0x20010000 : 0x1cull << 40, i < 34 ? 0 : 0x1234 );
    for (i = 40; i < 80; i++) sample( i, 24, i < 70 ? 0 : 3, i < 70 ? 0x4b0000 : 0x1cull << 40, 0 );
    for (i = 80; i < 85; i++) sample( i, 12, 0, 0x4c0000, 0 );
    frame( 4, 16.7, 0 );
    frame( 4, 16.7, 0 );
    prof_count = 85;
    frame( 4, 80, 0 );
    prof_count = 0;
    assert( report() > 0 );
    contains( "\n[FRAMES] slow frame 80.0 ms: card 0 ms, 0 shaders compiled in 0 ms, its thread in exe 48 ms, "
              "gamex86.dll 20 ms, svc0x1c in server_call_unlocked<NtWaitForSingleObject 12 ms (40 samples), "
              "thread 24 in exe 60 ms, svc0x1c<condvarWaitTimeout 20 ms (40 samples), present 0 ms\n" );
    assert( !strstr( line, "thread 12" ) );
    /* More samples than are kept: the last of them, at 2 ms each. */
    for (i = 0; i < 200; i++) sample( i, 4, 0, 0x4a0000, 0 );
    frame( 4, 16.7, 0 );
    prof_count = 200;
    frame( 4, 400, 0 );
    prof_count = 0;
    assert( report() > 0 );
    contains( "slow frame 400.0 ms: card 0 ms, 0 shaders compiled in 0 ms, its thread in exe 320 ms "
              "(the last 160 samples), present 0 ms" );
    /* Only frames among the slowest ask for samples. */
    prof_calls = 0;
    frame( 4, 16.7, 0 );
    for (i = 0; i < 20; i++) frame( 4, 16.7, 0 );
    assert( prof_calls == 3 );
    report();
}

static void with_server_calls( void )
{
    unsigned int i;

    /* A frame of 64 ms in which the presenting thread waited 44 ms for an
     * event through the server's thread, and 5 ms for a message; a third
     * call of the frame is not shown. */
    calls_given[0] = (struct wine_nx_slow_call){ .start = 0, .end = 44 * TICKS_PER_MS, .name = "select",
        .callers = 2, .caller = { 0x50e50f, 0x50d38c },
        .how = "by the server's thread as it carried a thread context: wait for 1: event; no timeout" };
    calls_given[1] = (struct wine_nx_slow_call){ .start = 0, .end = 5 * TICKS_PER_MS, .name = "get_message",
        .how = "by the server's thread" };
    frame( 4, 16.7, 0 );
    frame( 4, 16.7, 0 );
    call_count = 2;
    call_total = 3;
    frame( 4, 64, 0 );
    call_count = call_total = 0;
    assert( report() > 0 );
    contains( "\n[FRAMES] slow frame 64.0 ms: card 0 ms, 0 shaders compiled in 0 ms, present 0 ms\n"
              "[FRAMES]  its server calls of 4 ms or more: 3, select 44 ms by the server's thread as it carried "
              "a thread context: wait for 1: event; no timeout, from exe+0x10e50f<exe+0x10d38c; get_message 5 ms "
              "by the server's thread\n" );
    /* The frames with none have no line for them. */
    assert( !strstr( strstr( line, "server calls" ) + 1, "server calls" ) );
    contains( "\n[FRAMES] slow frame 16.7 ms: card 0 ms, 0 shaders compiled in 0 ms, present 0 ms" );
    /* Only frames among the slowest ask for them. */
    call_asks = 0;
    frame( 4, 16.7, 0 );
    for (i = 0; i < 20; i++) frame( 4, 16.7, 0 );
    assert( call_asks == 3 );
    report();
}

static void many( void )
{
    unsigned int i;

    /* More frames than are kept for the percentiles: all are counted, and the
     * slowest three are in order. */
    frame( 4, 16.7, 0 );
    for (i = 0; i < FRAMES_KEPT + 100; i++) frame( 4, i == 4000 ? 300 : i % 97 == 0 ? 50 + i % 7 : 8.3, 0 );
    assert( report() > 0 );
    contains( "[FRAMES] 4197 in " );
    contains( "median 8.3 ms" );
    contains( "slow frame 300.0 ms:" );
    contains( "slow frame 56.0 ms:" );
}

int main( void )
{
    without_fex();
    with_fex();
    with_gl_thread();
    with_samples();
    with_server_calls();
    many();
    printf( "frame_pacing: all passed\n" );
    return 0;
}
