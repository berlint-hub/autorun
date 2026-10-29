/* Host test for which OpenGL renderer answered (source/nxgl_renderer.h).
 *
 * The renderer a program asked for is its setting; the renderer it got is the
 * GL_RENDERER of its own context, and the two are not always the same: an EGL
 * display that was made before the setting keeps its backend. These are the
 * strings the two drivers really give. */
#include <assert.h>
#include <stdio.h>

#include "../source/nxgl_renderer.h"

static void check_is_zink( void )
{
    assert( nxgl_renderer_is_zink( "zink" ) );
    assert( nxgl_renderer_is_zink( "Zink" ) );
    assert( nxgl_renderer_is_zink( "zink (Mesa)" ) );
    assert( nxgl_renderer_is_zink( "zink Vulkan 1.4 (NVIDIA Tegra X1 (NVK VK_KHR_swapchain))" ) );
    /* The card's own names, which is what NVC0 answers with. */
    assert( !nxgl_renderer_is_zink( "NV132" ) );
    assert( !nxgl_renderer_is_zink( "NVIDIA Tegra X1" ) );
    assert( !nxgl_renderer_is_zink( "NVE4" ) );
    /* Nothing at all, and words that only start the same way. */
    assert( !nxgl_renderer_is_zink( "" ) );
    assert( !nxgl_renderer_is_zink( NULL ) );
    assert( !nxgl_renderer_is_zink( "zinking" ) );
    assert( !nxgl_renderer_is_zink( "azink" ) );
    assert( !nxgl_renderer_is_zink( "zin" ) );
    assert( !nxgl_renderer_is_zink( "z" ) );
}

static void check_matches( void )
{
    /* Asked for Zink: only Zink is the answer. */
    assert( nxgl_renderer_matches( 1, "zink (Mesa)" ) );
    assert( !nxgl_renderer_matches( 1, "NV132" ) );
    /* Asked for native NVC0: only a driver that does not name itself Zink is. */
    assert( nxgl_renderer_matches( 0, "NV132" ) );
    assert( !nxgl_renderer_matches( 0, "zink Vulkan 1.4 (NVIDIA Tegra X1)" ) );
    /* A driver that answers with nothing contradicts nothing. */
    assert( nxgl_renderer_matches( 1, NULL ) );
    assert( nxgl_renderer_matches( 1, "" ) );
    assert( nxgl_renderer_matches( 0, NULL ) );
}

/* The value the runtime puts in the environment, as mesa-switch reads it. */
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
    check_is_zink();
    check_matches();
    check_requested();
    check_verdict();
    printf( "OpenGL renderer: which renderer answered, against the one asked for, passed\n" );
    return 0;
}
