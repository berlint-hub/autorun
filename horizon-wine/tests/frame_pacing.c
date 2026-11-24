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

#define TICKS_PER_MS 19200ull

/* FEX's area: the header, then the Windows threads 4 and 20. */
static struct
{
    FexStatsHeader header;
    FexThreadStats threads[2];
} fex_area;

static char line[1024];

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
    contains( "over twice the median 2, over 100 ms 1;" );
    contains( "slowest 120.0 ms: card 30 ms, 2 shaders compiled in 45 ms, present 3 ms;" );
    contains( "; 40.0 ms: card 0 ms, 0 shaders compiled in 0 ms, present 2 ms;" );
    contains( "; 30.0 ms:" );
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
    contains( "gaps of a second or more 1;" );
    contains( "slowest 16.7 ms:" );
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
    contains( "slowest 80.0 ms: FEX translating 50 ms (60 ms on all threads), card 0 ms" );
    contains( "; 60.0 ms: FEX translating 0 ms (7 ms on all threads)," );

    /* A broken area is left alone. */
    fex_area.header.thread_stats_size = 1;
    frame( 4, 16.7, 0 );
    frame( 4, 16.7, 0 );
    assert( report() > 0 );
    assert( !strstr( line, "FEX" ) );
    wine_nx_fex_stats_base = NULL;
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
    contains( "slowest 300.0 ms:" );
    contains( "; 56.0 ms:" );
}

int main( void )
{
    without_fex();
    with_fex();
    many();
    printf( "frame_pacing: all passed\n" );
    return 0;
}
