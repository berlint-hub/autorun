/* Host test for horizon-wine/source/display_sync.h: which panel rate a measured
 * frame rate asks for, how fast it may move, and what the measurement says.
 * Built by .github/workflows/build.yml ("Host tests"). */
#include <stdio.h>
#include <stdlib.h>

#include "display_sync.h"

static int failures;

static void expect_unsigned( const char *what, unsigned got, unsigned want )
{
    if (got == want) return;
    fprintf( stderr, "FAIL %s: got %u want %u\n", what, got, want );
    failures++;
}

static void expect_int( const char *what, int got, int want )
{
    if (got == want) return;
    fprintf( stderr, "FAIL %s: got %d want %d\n", what, got, want );
    failures++;
}

/* Rounding upward, because a 45 Hz panel cannot show 49 fps without dropping
 * one frame in nine. */
static void check_legal_rate(void)
{
    expect_unsigned( "1 fps", wine_nx_display_sync_legal_rate( 1 ), 45 );
    expect_unsigned( "44 fps", wine_nx_display_sync_legal_rate( 44 ), 45 );
    expect_unsigned( "45 fps", wine_nx_display_sync_legal_rate( 45 ), 45 );
    expect_unsigned( "46 fps", wine_nx_display_sync_legal_rate( 46 ), 50 );
    expect_unsigned( "49 fps", wine_nx_display_sync_legal_rate( 49 ), 50 );
    expect_unsigned( "50 fps", wine_nx_display_sync_legal_rate( 50 ), 50 );
    expect_unsigned( "51 fps", wine_nx_display_sync_legal_rate( 51 ), 55 );
    expect_unsigned( "55 fps", wine_nx_display_sync_legal_rate( 55 ), 55 );
    expect_unsigned( "56 fps", wine_nx_display_sync_legal_rate( 56 ), 60 );
    expect_unsigned( "60 fps", wine_nx_display_sync_legal_rate( 60 ), 60 );
    expect_unsigned( "144 fps", wine_nx_display_sync_legal_rate( 144 ), 60 );
    expect_unsigned( "no measurement", wine_nx_display_sync_legal_rate( 0 ), 0 );
}

static void check_next_rate(void)
{
    /* Nothing known yet: take the rate the measurement asks for. */
    expect_unsigned( "first", wine_nx_display_sync_next_rate( 0, 52 ), 55 );
    /* One step at a time, up and down. */
    expect_unsigned( "60 to 55", wine_nx_display_sync_next_rate( 60, 52 ), 55 );
    expect_unsigned( "55 to 50", wine_nx_display_sync_next_rate( 55, 47 ), 50 );
    expect_unsigned( "50 to 45", wine_nx_display_sync_next_rate( 50, 41 ), 45 );
    expect_unsigned( "45 to 50", wine_nx_display_sync_next_rate( 45, 52 ), 50 );
    /* Hovering just under the current rate is not a reason to step down. */
    expect_unsigned( "58 at 60", wine_nx_display_sync_next_rate( 60, 58 ), 60 );
    expect_unsigned( "57 at 60", wine_nx_display_sync_next_rate( 60, 57 ), 60 );
    /* A whole step below is. */
    expect_unsigned( "56 at 60", wine_nx_display_sync_next_rate( 60, 56 ), 60 );
    expect_unsigned( "55 at 60", wine_nx_display_sync_next_rate( 60, 55 ), 55 );
    /* A light game stays at the top: there is no 120 Hz handheld panel. */
    expect_unsigned( "200 at 60", wine_nx_display_sync_next_rate( 60, 200 ), 60 );
    /* No measurement leaves the panel alone rather than guessing. */
    expect_unsigned( "no measurement", wine_nx_display_sync_next_rate( 55, 0 ), 55 );
    /* A rate this policy did not choose (a hand-edited shared block) has no
     * step to take from it: the wanted rate is used as it is. */
    expect_unsigned( "47 at 52", wine_nx_display_sync_next_rate( 47, 52 ), 55 );
    expect_unsigned( "63 at 41", wine_nx_display_sync_next_rate( 63, 41 ), 45 );
}

static void check_stability(void)
{
    unsigned candidate = 0;
    uint64_t since = 0;
    const uint64_t two_seconds = 2 * 19200000ull;

    expect_int( "first request", wine_nx_display_sync_stable( 55, &candidate, &since, 1000, two_seconds ), 0 );
    expect_int( "still waiting", wine_nx_display_sync_stable( 55, &candidate, &since, two_seconds, two_seconds ), 0 );
    expect_int( "held long enough", wine_nx_display_sync_stable( 55, &candidate, &since, 1000 + two_seconds, two_seconds ), 1 );
    /* A different target starts the wait again. */
    expect_int( "new target", wine_nx_display_sync_stable( 50, &candidate, &since, 1000 + two_seconds + 5, two_seconds ), 0 );
    expect_int( "new target held", wine_nx_display_sync_stable( 50, &candidate, &since, 1000 + 2 * two_seconds + 5, two_seconds ), 1 );
    /* A clock that went backwards is not a stable target. */
    expect_int( "clock backwards", wine_nx_display_sync_stable( 50, &candidate, &since, 1, two_seconds ), 0 );
    expect_int( "no target", wine_nx_display_sync_stable( 0, &candidate, &since, 1, two_seconds ), 0 );
}

static void fill( uint64_t *intervals, size_t count, uint64_t value )
{
    size_t i;
    for (i = 0; i < count; i++) intervals[i] = value;
}

static void check_estimate(void)
{
    const uint64_t mhz = 19200000;   /* the console's system tick, for scale */
    uint64_t intervals[WINE_NX_DISPLAY_SYNC_SAMPLES];
    struct wine_nx_display_sync_estimate estimate;

    /* A steady 60 Hz: 16.67 ms a frame. */
    fill( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, mhz / 60 );
    estimate = wine_nx_display_sync_estimate( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, mhz,
                                              WINE_NX_DISPLAY_SYNC_MIN_SAMPLES );
    expect_unsigned( "60 steady average", estimate.average_fps, 60 );
    expect_unsigned( "60 steady upper", estimate.upper_fps, 60 );

    /* A steady 50 Hz. */
    fill( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, mhz / 50 );
    estimate = wine_nx_display_sync_estimate( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, mhz,
                                              WINE_NX_DISPLAY_SYNC_MIN_SAMPLES );
    expect_unsigned( "50 steady average", estimate.average_fps, 50 );
    expect_unsigned( "50 steady upper", estimate.upper_fps, 50 );

    /* 59.94 Hz is 16.683 ms a frame, not 16.667: the estimate is a whole number
     * of frames a second and may say 59 or 60, but it must not fall to 55. */
    fill( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, mhz * 16683 / 1000000 );
    estimate = wine_nx_display_sync_estimate( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, mhz,
                                              WINE_NX_DISPLAY_SYNC_MIN_SAMPLES );
    expect_int( "59.94 upper is 59 or 60", estimate.upper_fps >= 59 && estimate.upper_fps <= 60, 1 );
    expect_unsigned( "59.94 asks for 60", wine_nx_display_sync_legal_rate( estimate.upper_fps ), 60 );

    /* One long frame in a run of 60s: the average drops, the upper rate -- the
     * one the panel follows -- does not, so a single shader compile does not
     * take the panel down with it. */
    fill( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, mhz / 60 );
    intervals[7] = mhz / 4;
    estimate = wine_nx_display_sync_estimate( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, mhz,
                                              WINE_NX_DISPLAY_SYNC_MIN_SAMPLES );
    expect_unsigned( "one hitch upper", estimate.upper_fps, 60 );
    expect_int( "one hitch average drops", estimate.average_fps < 60, 1 );

    /* Too few samples: nothing is published from a load screen. */
    fill( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, mhz / 60 );
    estimate = wine_nx_display_sync_estimate( intervals, WINE_NX_DISPLAY_SYNC_MIN_SAMPLES - 1, mhz,
                                              WINE_NX_DISPLAY_SYNC_MIN_SAMPLES );
    expect_unsigned( "too few samples", estimate.upper_fps, 0 );
    expect_unsigned( "too few average", estimate.average_fps, 0 );

    /* No ticks, no measurement. */
    estimate = wine_nx_display_sync_estimate( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, 0,
                                              WINE_NX_DISPLAY_SYNC_MIN_SAMPLES );
    expect_unsigned( "no tick rate", estimate.upper_fps, 0 );

    /* A zero interval is a hole in the window, not an infinite frame rate. */
    fill( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, mhz / 60 );
    intervals[3] = 0;
    estimate = wine_nx_display_sync_estimate( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, mhz,
                                              WINE_NX_DISPLAY_SYNC_MIN_SAMPLES );
    expect_unsigned( "zero interval", estimate.upper_fps, 0 );

    /* Absurd intervals are clamped away rather than trusted. */
    fill( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, mhz / 2 );
    estimate = wine_nx_display_sync_estimate( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, mhz,
                                              WINE_NX_DISPLAY_SYNC_MIN_SAMPLES );
    expect_unsigned( "2 fps is not a rate", estimate.upper_fps, 0 );

    /* A window longer than the array is truncated, not read past. */
    fill( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES, mhz / 60 );
    estimate = wine_nx_display_sync_estimate( intervals, WINE_NX_DISPLAY_SYNC_SAMPLES + 100, mhz,
                                              WINE_NX_DISPLAY_SYNC_MIN_SAMPLES );
    expect_unsigned( "long window upper", estimate.upper_fps, 60 );
}

/* The whole way round: a game that settles at 50 fps ends on a 50 Hz panel in
 * one step, and stays there while it keeps drawing 50. */
static void check_walk(void)
{
    const uint64_t mhz = 19200000, stable = (uint64_t)WINE_NX_DISPLAY_SYNC_STABLE_MS * mhz / 1000;
    unsigned rate = 60, candidate = 0;
    uint64_t since = 0, now = 1;
    int i;

    for (i = 0; i < 4; i++)
    {
        unsigned next = wine_nx_display_sync_next_rate( rate, 50 );
        if (!wine_nx_display_sync_stable( next, &candidate, &since, now, stable )) return;
        rate = next;
        now += stable;
    }
    expect_unsigned( "walked to 50", rate, 50 );
}

int main( void )
{
    check_legal_rate();
    check_next_rate();
    check_stability();
    check_estimate();
    check_walk();

    if (failures)
    {
        fprintf( stderr, "%d display sync checks failed\n", failures );
        return 1;
    }
    puts( "display sync: panel rate policy, stability and measurement passed" );
    return 0;
}
