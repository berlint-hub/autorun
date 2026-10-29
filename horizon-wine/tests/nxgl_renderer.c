/* Host test for which OpenGL renderer answered (source/nxgl_renderer.h).
 *
 * The runtime asks mesa-switch for Zink or for NVC0 per program, but the answer
 * comes from whatever display was made first in the process. dlls/win32u/
 * winnx_opengl.c compares the two so that a program running on a renderer it
 * did not ask for says so, instead of a setting that looks applied. The strings
 * below are the ones the drivers really hand out. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../source/nxgl_renderer.h"

/* The variable as mesa-switch reads it: "zink" is Zink, everything else is the
 * display's own default. */
static void check_requested( void )
{
    assert( nxgl_renderer_requested( "zink" ) == NXGL_RENDERER_ZINK );
    assert( nxgl_renderer_requested( "ZINK" ) == NXGL_RENDERER_ZINK );
    assert( nxgl_renderer_requested( "nvc0" ) == NXGL_RENDERER_NVC0 );
    assert( nxgl_renderer_requested( "nouveau" ) == NXGL_RENDERER_NVC0 );
    /* Unset, empty and unknown are all the display's default: mesa-switch warns
     * about an unknown name and picks the default, so the log must not claim a
     * renderer was asked for when none was. */
    assert( nxgl_renderer_requested( NULL ) == NXGL_RENDERER_NVC0 );
    assert( nxgl_renderer_requested( "" ) == NXGL_RENDERER_NVC0 );
    assert( nxgl_renderer_requested( "zink2" ) == NXGL_RENDERER_NVC0 );
    assert( nxgl_renderer_requested( "nvc0zink" ) == NXGL_RENDERER_NVC0 );
}

/* What GL_RENDERER says, from the two drivers as they name themselves. */
static void check_renderer_strings( void )
{
    assert( nxgl_renderer_is_zink( "zink" ) );
    assert( nxgl_renderer_is_zink( "zink (Mesa)" ) );
    assert( nxgl_renderer_is_zink( "Zink Vulkan 1.4 (NVIDIA Tegra X1 (NVK VK_KHR_...))" ) );
    assert( !nxgl_renderer_is_zink( "zinking" ) );
    assert( !nxgl_renderer_is_zink( "NV132" ) );
    assert( !nxgl_renderer_is_zink( "NVIDIA Tegra X1" ) );
    assert( !nxgl_renderer_is_zink( "" ) );
    assert( !nxgl_renderer_is_zink( NULL ) );
}

/* The comparison the driver logs: asked Zink and got Zink is as asked, asked
 * Zink and got NVC0 is a display that was made first with another backend, and
 * a driver that answers with nothing is neither. */
static void check_verdict( void )
{
    assert( nxgl_renderer_state_of( NXGL_RENDERER_ZINK, "zink (Mesa)" ) == NXGL_RENDERER_AS_ASKED );
    assert( nxgl_renderer_state_of( NXGL_RENDERER_ZINK, "NV132" ) == NXGL_RENDERER_OTHER );
    assert( nxgl_renderer_state_of( NXGL_RENDERER_NVC0, "NV132" ) == NXGL_RENDERER_AS_ASKED );
    assert( nxgl_renderer_state_of( NXGL_RENDERER_NVC0, "zink Vulkan 1.4 (NVIDIA Tegra X1)" )
            == NXGL_RENDERER_OTHER );
    assert( nxgl_renderer_state_of( NXGL_RENDERER_ZINK, NULL ) == NXGL_RENDERER_NO_ANSWER );
    assert( nxgl_renderer_state_of( NXGL_RENDERER_ZINK, "" ) == NXGL_RENDERER_NO_ANSWER );
}

int main(void)
{
    check_requested();
    check_renderer_strings();
    check_verdict();
    printf( "OpenGL renderer: requested, named and compared against the answer passed\n" );
    return 0;
}
