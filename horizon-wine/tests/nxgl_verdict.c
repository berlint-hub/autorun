/* Host test for horizon-wine/source/nxgl_verdict.h: which of the ways a screen
 * can show no frame a stuck OpenGL swap is, and the line that says it.
 * Built by .github/workflows/build.yml ("Host tests"). */
#include <stdio.h>
#include <string.h>

#include "nxgl_verdict.h"

static int failures;

static struct nxgl_swap_state healthy( void )
{
    struct nxgl_swap_state s;

    /* A stuck swap on a screen that looks right in every way: the case that
     * blames the driver, and the base the other cases are broken from. */
    memset( &s, 0, sizeof(s) );
    s.stuck_ms = 12000;
    s.owner = NXGL_OWNER_GL;
    s.surface_has_screen = 1;
    s.surface_w = 800;
    s.surface_h = 600;
    s.window_w = 800;
    s.window_h = 600;
    s.slots = 2;
    s.slot_out = 0;
    s.connected = 1;
    return s;
}

static void expect_verdict( const char *what, struct nxgl_swap_state s, enum nxgl_verdict want )
{
    enum nxgl_verdict got = nxgl_swap_verdict( &s );

    if (got == want) return;
    fprintf( stderr, "FAIL %s: verdict %d, want %d\n", what, got, want );
    failures++;
}

static void expect_contains( const char *what, struct nxgl_swap_state s, const char *a, const char *b )
{
    char line[512];

    if (!nxgl_swap_report( line, sizeof(line), &s ))
    {
        fprintf( stderr, "FAIL %s: no line for a stuck swap\n", what );
        failures++;
        return;
    }
    if (a && !strstr( line, a ))
    {
        fprintf( stderr, "FAIL %s: \"%s\" does not say \"%s\"\n", what, line, a );
        failures++;
    }
    if (b && !strstr( line, b ))
    {
        fprintf( stderr, "FAIL %s: \"%s\" does not say \"%s\"\n", what, line, b );
        failures++;
    }
}

int main(void)
{
    struct nxgl_swap_state s;
    char line[512];

    /* A swap that is only slow says nothing: the report is for stalls. */
    s = healthy();
    s.stuck_ms = NXGL_SWAP_STUCK_MIN_MS - 1;
    expect_verdict( "slow swap, not stuck", s, NXGL_SWAP_NOT_STUCK );
    if (nxgl_swap_report( line, sizeof(line), &s ))
    {
        fprintf( stderr, "FAIL slow swap: a line was written below the threshold\n" );
        failures++;
    }
    s.stuck_ms = NXGL_SWAP_STUCK_MIN_MS;
    expect_verdict( "at the threshold", s, NXGL_SWAP_DRIVER );

    /* The five ways it can be somebody else's fault, and the one where it is
     * the driver's. */
    s = healthy();
    s.surface_has_screen = 0;
    s.owner = NXGL_OWNER_VULKAN;
    expect_verdict( "surface without the screen", s, NXGL_SWAP_NO_SCREEN );
    expect_contains( "surface without the screen", s, "a Vulkan surface", "offscreen" );

    /* The publisher only publishes from a swap that had the screen when it
     * began, so the live owner is what says whether it still does: a Vulkan
     * surface taking the screen with an OpenGL swap open leaves that present
     * nowhere to go, and that is a different bug from the driver hanging. */
    s = healthy();
    s.owner = NXGL_OWNER_VULKAN;
    expect_verdict( "screen taken by Vulkan", s, NXGL_SWAP_NO_SCREEN );
    expect_contains( "screen taken by Vulkan", s, "went to a Vulkan surface", "no present can finish" );
    s = healthy();
    s.owner = NXGL_OWNER_COMPOSITOR;
    expect_verdict( "screen back to the compositor", s, NXGL_SWAP_NO_SCREEN );
    expect_contains( "screen back to the compositor", s, "went to the compositor", NULL );
    s = healthy();
    s.owner = NXGL_OWNER_NONE;
    expect_verdict( "screen given back", s, NXGL_SWAP_NO_SCREEN );
    expect_contains( "screen given back", s, "given back since it began", NULL );

    s = healthy();
    s.connected = 0;
    expect_verdict( "disconnected screen", s, NXGL_SWAP_NOT_CONNECTED );

    s = healthy();
    s.slots = 0;
    expect_verdict( "no slots registered", s, NXGL_SWAP_NO_SLOTS );
    expect_contains( "no slots registered", s, "no buffers registered", "800x600" );

    s = healthy();
    s.slot_out = 1;
    expect_verdict( "a buffer never returned", s, NXGL_SWAP_SLOT_OUT );
    expect_contains( "a buffer never returned", s, "has not come back", "2 slots" );

    /* Slots configured by somebody else's surface: the compositor's window the
     * program asked to resize, which is what Dark Athena did. */
    s = healthy();
    s.window_w = 1280;
    s.window_h = 720;
    expect_verdict( "slots at another size", s, NXGL_SWAP_OTHER_SIZE );
    expect_contains( "slots at another size", s, "1280x720", "800x600" );

    s = healthy();
    expect_verdict( "everything right", s, NXGL_SWAP_DRIVER );
    /* Twelve seconds, not the threshold: the time in the line is the state's,
     * and the numbers the verdict was built on are in it too. */
    expect_contains( "everything right", s, "12.0 s", "driver itself did not return" );
    expect_contains( "everything right", s, "800x600", "2 slots" );

    /* A size unknown on one side is not a mismatch: a query that failed must
     * not turn into a wrong verdict. */
    s = healthy();
    s.surface_w = s.surface_h = 0;
    expect_verdict( "unknown surface size", s, NXGL_SWAP_DRIVER );
    s = healthy();
    s.window_w = s.window_h = 0;
    expect_verdict( "unknown window size", s, NXGL_SWAP_DRIVER );

    /* The owner is named in the cases where it matters. */
    s = healthy();
    s.surface_has_screen = 0;
    s.owner = NXGL_OWNER_COMPOSITOR;
    expect_contains( "compositor owns the screen", s, "compositor", NULL );
    s.owner = NXGL_OWNER_NONE;
    expect_contains( "nobody owns the screen", s, "nothing", NULL );

    if (failures)
    {
        fprintf( stderr, "%d failure(s)\n", failures );
        return 1;
    }
    puts( "PASS: every way a stuck swap can leave the screen empty is told apart, and named with its numbers" );
    return 0;
}
