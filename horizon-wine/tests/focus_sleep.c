/*
 * The console's focus: what a run does with it.
 *
 * These are the cases that matter to a game that is running when the console
 * goes to sleep. The interesting ones are the doubles: a second notice while
 * the console is already away must not release the screen twice, and a screen
 * that will not come back ends the run rather than leaving a game nobody can
 * see on a panel nobody can read.
 */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "focus_sleep.h"

static void test_sleep_and_wake(void)
{
    struct focus_sleep f;
    char line[256];

    focus_sleep_init( &f, FOCUS_STATE_IN_FOCUS );
    assert( !f.sleeps && !f.returns && !f.released && !f.gave_up );

    /* The console goes away with the screen in hand: the screen goes back. */
    assert( focus_sleep_change( &f, FOCUS_STATE_BACKGROUND, 1 ) == FOCUS_STEP_RELEASE );
    assert( !f.in_focus && f.released && f.sleeps == 1 );
    /* It comes back: the screen is taken again. */
    assert( focus_sleep_change( &f, FOCUS_STATE_IN_FOCUS, 1 ) == FOCUS_STEP_REACQUIRE );
    assert( f.in_focus && !f.released && f.returns == 1 );
    focus_sleep_reacquired( &f );

    /* Nothing moved: nothing to do, and the log does not fill with it. */
    assert( focus_sleep_change( &f, FOCUS_STATE_IN_FOCUS, 1 ) == FOCUS_STEP_NONE );
    /* Already away, and told again: not a second sleep, and the screen is not
     * released a second time -- it is already gone. */
    assert( focus_sleep_change( &f, FOCUS_STATE_BACKGROUND, 1 ) == FOCUS_STEP_RELEASE );
    assert( f.sleeps == 2 );
    assert( focus_sleep_change( &f, FOCUS_STATE_NOT_FOCUSED, 1 ) == FOCUS_STEP_NONE );
    assert( focus_sleep_change( &f, FOCUS_STATE_BACKGROUND, 1 ) == FOCUS_STEP_NONE );
    assert( f.sleeps == 2 );

    /* A console that never had the screen: the launcher's own loop owns it,
     * and this code leaves it alone. */
    {
        struct focus_sleep g;

        focus_sleep_init( &g, FOCUS_STATE_IN_FOCUS );
        assert( focus_sleep_change( &g, FOCUS_STATE_BACKGROUND, 0 ) == FOCUS_STEP_NONE );
        assert( g.sleeps == 1 && !g.released );
        assert( focus_sleep_change( &g, FOCUS_STATE_IN_FOCUS, 0 ) == FOCUS_STEP_REACQUIRE );
    }

    /* Three sleeps and three returns are all counted: a run that was put away
     * and brought back all evening says so once, in its report. The console is
     * still away from the case above, so it wakes first. */
    {
        int i;

        assert( focus_sleep_change( &f, FOCUS_STATE_IN_FOCUS, 1 ) == FOCUS_STEP_REACQUIRE );
        focus_sleep_reacquired( &f );
        for (i = 0; i < 3; i++)
        {
            assert( focus_sleep_change( &f, FOCUS_STATE_BACKGROUND, 1 ) == FOCUS_STEP_RELEASE );
            assert( focus_sleep_change( &f, FOCUS_STATE_IN_FOCUS, 1 ) == FOCUS_STEP_REACQUIRE );
            focus_sleep_reacquired( &f );
        }
        assert( f.sleeps == 5 && f.returns == 5 );
    }

    assert( focus_sleep_describe( &f, line, sizeof(line) ) > 0 );
    assert( strstr( line, "went away 5 time(s)" ) && strstr( line, "came back" ) );
    /* A run that never slept says nothing about it. */
    focus_sleep_init( &f, FOCUS_STATE_IN_FOCUS );
    assert( focus_sleep_describe( &f, line, sizeof(line) ) == 0 );
}

static void test_a_screen_that_will_not_come_back(void)
{
    struct focus_sleep f;
    char line[256];
    int i;

    focus_sleep_init( &f, FOCUS_STATE_IN_FOCUS );
    assert( focus_sleep_change( &f, FOCUS_STATE_BACKGROUND, 1 ) == FOCUS_STEP_RELEASE );

    /* One failed return is not enough to give up: a console waking from sleep
     * hands the display back over several frames. */
    assert( focus_sleep_change( &f, FOCUS_STATE_IN_FOCUS, 1 ) == FOCUS_STEP_REACQUIRE );
    assert( !focus_sleep_reacquire_failed( &f ) );
    assert( focus_sleep_change( &f, FOCUS_STATE_BACKGROUND, 1 ) == FOCUS_STEP_RELEASE );
    assert( focus_sleep_change( &f, FOCUS_STATE_IN_FOCUS, 1 ) == FOCUS_STEP_REACQUIRE );
    assert( !focus_sleep_reacquire_failed( &f ) );

    /* The third does. */
    assert( focus_sleep_change( &f, FOCUS_STATE_BACKGROUND, 1 ) == FOCUS_STEP_RELEASE );
    assert( focus_sleep_change( &f, FOCUS_STATE_IN_FOCUS, 1 ) == FOCUS_STEP_REACQUIRE );
    assert( focus_sleep_reacquire_failed( &f ) );
    assert( f.gave_up );

    /* And the next return ends the run instead of trying again. */
    assert( focus_sleep_change( &f, FOCUS_STATE_BACKGROUND, 1 ) == FOCUS_STEP_RELEASE );
    assert( focus_sleep_change( &f, FOCUS_STATE_IN_FOCUS, 1 ) == FOCUS_STEP_END_RUN );

    assert( focus_sleep_describe( &f, line, sizeof(line) ) > 0 );
    assert( strstr( line, "would not come back" ) && strstr( line, "the run was ended" ) );

    /* A screen that comes back after all clears it: the count starts again, so
     * one bad wake on a long evening does not end the next run's first sleep. */
    focus_sleep_reacquired( &f );
    assert( !f.gave_up && !f.failures );
    for (i = 0; i < FOCUS_SLEEP_RETRIES - 1; i++)
        assert( !focus_sleep_reacquire_failed( &f ) );
    assert( focus_sleep_reacquire_failed( &f ) );
}

int main(void)
{
    test_sleep_and_wake();
    test_a_screen_that_will_not_come_back();
    puts( "Console focus: a sleep giving the screen back, a wake taking it again, a console already "
          "away not being released twice, the launcher's own screen left alone, and a screen that "
          "will not come back ending the run passed" );
    return 0;
}
