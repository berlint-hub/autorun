/*
 * What the console's focus does to a run.
 *
 * Horizon tells an application that it is being put to sleep, or that the HOME
 * menu is coming up, by taking its focus away. libnx's default is
 * AppletFocusHandlingMode_SuspendHomeSleep: the process is frozen where it
 * stands and told nothing. That is what Autorun has always used, and nothing
 * in the tree ever handled a focus change -- appletMainLoop() ran only in the
 * launcher's loop, so while a game was running the messages were not even
 * read. A console put to sleep in a game therefore froze mid-present, holding
 * the framebuffer and whatever the GPU had out, and came back to a display
 * nobody owned: the last frame stays on the panel and nothing answers. The
 * user's words are "zmrzlý obraz ... musím vypnout".
 *
 * This is the part that decides what to do, kept apart from the Switch calls
 * so it can be tested on a host. The actions themselves are the runtime's.
 *
 * The mode the runtime asks for is AppletFocusHandlingMode_SuspendHomeSleepNotify:
 * the console still sleeps, but the hook is called first, which is the only
 * chance to give the screen back before the freeze. It is called for the HOME
 * menu as well, so releasing must be safe to do and to undo.
 */
#ifndef WINE_NX_FOCUS_SLEEP_H
#define WINE_NX_FOCUS_SLEEP_H

#include <stddef.h>
#include <stdio.h>

/* libnx's AppletFocusState. */
#define FOCUS_STATE_IN_FOCUS    1
#define FOCUS_STATE_NOT_FOCUSED 2
#define FOCUS_STATE_BACKGROUND  3

/* How many times the screen may fail to come back before the run is ended.
 * One is not enough: a console waking from sleep hands the display back over
 * several frames. */
#define FOCUS_SLEEP_RETRIES 3

enum focus_step
{
    FOCUS_STEP_NONE,       /* nothing to do; the log says why not */
    FOCUS_STEP_RELEASE,    /* give the screen back before the freeze */
    FOCUS_STEP_REACQUIRE,  /* the console is awake; take it again */
    FOCUS_STEP_END_RUN     /* the screen did not come back; end the run */
};

struct focus_sleep
{
    int in_focus;             /* what the console last said */
    int released;             /* this code gave the screen back and has not taken it again */
    unsigned int sleeps;      /* times the console went away */
    unsigned int returns;     /* times it came back */
    unsigned int failures;    /* returns on which the screen would not come back */
    unsigned int gave_up;     /* the run is to be ended on the next return */
};

/* state is libnx's AppletFocusState; this remembers whether it was in focus. */
static inline void focus_sleep_init( struct focus_sleep *f, int state )
{
    f->in_focus = state == FOCUS_STATE_IN_FOCUS;
    f->released = 0;
    f->sleeps = 0;
    f->returns = 0;
    f->failures = 0;
    f->gave_up = 0;
}

/* The console's focus moved. holds_screen is whether this program has the
 * display at all: in the launcher it is the launcher's own loop that owns it,
 * and this code leaves it alone. */
static inline enum focus_step focus_sleep_change( struct focus_sleep *f, int state, int holds_screen )
{
    int in_focus = state == FOCUS_STATE_IN_FOCUS;

    if (!in_focus && f->in_focus)
    {
        f->in_focus = 0;
        f->sleeps++;
        /* A second notice while already away is the console settling, not a
         * second sleep: the screen is gone, and releasing it again would be
         * releasing nothing. */
        if (!holds_screen) return FOCUS_STEP_NONE;
        f->released = 1;
        return FOCUS_STEP_RELEASE;
    }
    if (in_focus && !f->in_focus)
    {
        f->in_focus = 1;
        f->returns++;
        if (f->gave_up) return FOCUS_STEP_END_RUN;
        f->released = 0;
        return FOCUS_STEP_REACQUIRE;
    }
    return FOCUS_STEP_NONE;
}

/* The screen did not come back on a return. Returns 1 once the run should be
 * ended instead of trying again, and only then: a game that cannot be seen is
 * worse than one that is closed, and the report says which it was. */
static inline int focus_sleep_reacquire_failed( struct focus_sleep *f )
{
    if (++f->failures < FOCUS_SLEEP_RETRIES) return 0;
    f->gave_up = 1;
    return 1;
}

/* The screen came back after all. */
static inline void focus_sleep_reacquired( struct focus_sleep *f )
{
    f->failures = 0;
    f->gave_up = 0;
    f->released = 0;
}

/* One line for the run's report. */
static inline int focus_sleep_describe( const struct focus_sleep *f, char *out, size_t size )
{
    if (!f->sleeps) return 0;
    return snprintf( out, size,
                     "the console went away %u time(s) and came back %u; the screen was given back %s, "
                     "and %s",
                     f->sleeps, f->returns, f->released ? "and not taken again" : "and taken again",
                     f->gave_up ? "would not come back, so the run was ended"
                                : f->failures ? "came back after more than one try"
                                              : "came back" );
}

#endif /* WINE_NX_FOCUS_SLEEP_H */
