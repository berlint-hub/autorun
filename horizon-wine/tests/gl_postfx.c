/* The post-processing passes of an OpenGL program's frame (source/gl_postfx.h),
 * run over Mesa on the host: a compatibility context, as old games make, in
 * a pbuffer of the surfaceless platform.
 *
 *   cc -std=gnu11 -Wall -Wextra -Werror -O1 -g -I source tests/gl_postfx.c -lEGL -lm -o /tmp/gl_postfx
 *
 * Without a GL driver (no EGL display, or no pbuffer config) it says so and
 * passes: the host may have none.
 */
#include <assert.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "gl_postfx.h"

#define W 96
#define H 64

static struct gl_postfx_funcs f;
static void (*glClearColor_)( float, float, float, float );
static void (*glClear_)( unsigned int );
static void (*glReadPixels_)( int, int, int, int, unsigned int, unsigned int, void * );
static void (*glBegin_)( unsigned int );
static void (*glEnd_)( void );
static void (*glVertex2f_)( float, float );
static void (*glColor3f_)( float, float, float );
static void (*glScissor_)( int, int, int, int );
static void (*glGenSamplers_)( int, unsigned int * );
static void (*glFinish_)( void );

static void *get( const char *name )
{
    return (void *)eglGetProcAddress( name );
}

static unsigned char pixels[H][W][4];

static void read_back( void )
{
    glReadPixels_( 0, 0, W, H, GLPFX_RGBA, GLPFX_UNSIGNED_BYTE, pixels );
}

/* A white triangle over black, with a long shallow edge, drawn without any
 * smoothing: only black and white pixels. */
static void draw_edge( void )
{
    glClearColor_( 0, 0, 0, 1 );
    glClear_( GLPFX_COLOR_BUFFER_BIT );
    glColor3f_( 1, 1, 1 );
    glBegin_( GLPFX_TRIANGLES );
    glVertex2f_( -1.0f, -1.0f );
    glVertex2f_( 1.0f, -0.6f );
    glVertex2f_( -1.0f, 0.9f );
    glEnd_();
}

static unsigned int grey_pixels( void )
{
    unsigned int count = 0;

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            count += pixels[y][x][0] > 8 && pixels[y][x][0] < 247;
    return count;
}

static void no_error( void )
{
    unsigned int error = f.GetError();

    if (error) fprintf( stderr, "GL error 0x%x\n", error );
    assert( !error );
}

static void test_fxaa( struct gl_postfx *fx )
{
    char error[512];
    unsigned int before, after;

    draw_edge();
    read_back();
    before = grey_pixels();
    assert( gl_postfx_apply( &f, fx, W, H, GL_POSTFX_AA_FXAA, 0, error, sizeof(error) ) );
    if (error[0]) fprintf( stderr, "%s\n", error );
    no_error();
    read_back();
    after = grey_pixels();
    printf( "FXAA: %u grey pixels along the edge, %u before\n", after, before );
    assert( before == 0 && after > 20 );
    /* Far from the edge nothing moves. */
    assert( pixels[2][2][0] == 255 && pixels[H - 2][W - 2][0] == 0 );
}

static void test_rcas( struct gl_postfx *fx )
{
    char error[512];
    unsigned char before[W];

    /* A soft step: columns from dark to light over eight pixels. */
    for (int x = 0; x < W; x++)
    {
        float v = x < 44 ? 0.25f : x >= 52 ? 0.75f : 0.25f + (x - 44) * (0.5f / 8);

        glScissor_( x, 0, 1, H );
        f.Enable( GLPFX_SCISSOR_TEST );
        glClearColor_( v, v, v, 1 );
        glClear_( GLPFX_COLOR_BUFFER_BIT );
    }
    f.Disable( GLPFX_SCISSOR_TEST );
    read_back();
    for (int x = 0; x < W; x++) before[x] = pixels[H / 2][x][0];
    assert( gl_postfx_apply( &f, fx, W, H, GL_POSTFX_AA_OFF, gl_postfx_rcas_strength( 1.0f ), error, sizeof(error) ) );
    no_error();
    read_back();
    printf( "RCAS: the slope's ends %u %u | %u %u, before %u %u | %u %u\n",
            pixels[H / 2][43][0], pixels[H / 2][44][0], pixels[H / 2][52][0], pixels[H / 2][53][0],
            before[43], before[44], before[52], before[53] );
    /* Flat parts and the even slope stay as they were; where the slope meets
     * them the dark end gets darker and the light end lighter, within what
     * RCAS allows. */
    assert( abs( pixels[H / 2][10][0] - before[10] ) <= 1 && abs( pixels[H / 2][80][0] - before[80] ) <= 1 );
    assert( abs( pixels[H / 2][48][0] - before[48] ) <= 1 );
    assert( pixels[H / 2][44][0] < before[44] - 4 && pixels[H / 2][52][0] > before[52] + 4 );
    assert( pixels[H / 2][44][0] >= 40 && pixels[H / 2][52][0] <= 215 );
    /* 0 is none at all. */
    assert( !gl_postfx_apply( &f, fx, W, H, GL_POSTFX_AA_OFF, gl_postfx_rcas_strength( 0 ), error, sizeof(error) ) );
}

/* A game in a mode below the screen's draws its picture into the bottom-left
 * corner of a surface of the screen's size: EASU enlarges it over the whole
 * surface. A hard edge between two greys in a picture of half the size. */
static void draw_corner_picture( void )
{
    /* What is outside the picture is the game's to leave: red, overwritten. */
    f.Disable( GLPFX_SCISSOR_TEST );
    glClearColor_( 1, 0, 0, 1 );
    glClear_( GLPFX_COLOR_BUFFER_BIT );
    f.Enable( GLPFX_SCISSOR_TEST );
    glScissor_( 0, 0, W / 4, H / 2 );
    glClearColor_( 0.2f, 0.2f, 0.2f, 1 );
    glClear_( GLPFX_COLOR_BUFFER_BIT );
    glScissor_( W / 4, 0, W / 4, H / 2 );
    glClearColor_( 0.8f, 0.8f, 0.8f, 1 );
    glClear_( GLPFX_COLOR_BUFFER_BIT );
    f.Disable( GLPFX_SCISSOR_TEST );
}

static void test_easu( struct gl_postfx *fx )
{
    char error[512];
    int viewport[4];

    draw_corner_picture();
    f.Viewport( 0, 0, W / 2, H / 2 );  /* the game's own */
    assert( gl_postfx_apply_scaled( &f, fx, W, H, W / 2, H / 2, GL_POSTFX_AA_OFF, 1, 0, error, sizeof(error) ) );
    if (error[0]) fprintf( stderr, "%s\n", error );
    no_error();
    f.GetIntegerv( GLPFX_VIEWPORT, viewport );
    assert( viewport[0] == 0 && viewport[1] == 0 && viewport[2] == W / 2 && viewport[3] == H / 2 );
    f.Viewport( 0, 0, W, H );
    read_back();
    printf( "EASU: columns 45-50 of the surface %u %u %u %u %u %u, from a picture of %dx%d\n",
            pixels[H / 2][45][0], pixels[H / 2][46][0], pixels[H / 2][47][0], pixels[H / 2][48][0],
            pixels[H / 2][49][0], pixels[H / 2][50][0], W / 2, H / 2 );
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
        {
            const unsigned char *p = pixels[y][x];

            /* Every pixel of the surface is the picture's, grey, and every
             * row alike: nothing red is left. */
            assert( p[0] == p[1] && p[1] == p[2] && p[0] == pixels[0][x][0] );
            /* Flat parts stay flat, and the edge stays within its two greys:
             * EASU does not ring. */
            if (x <= 46) assert( abs( p[0] - 51 ) <= 1 );
            else if (x >= 49) assert( abs( p[0] - 204 ) <= 1 );
            else assert( p[0] > 52 && p[0] < 203 );
        }
    assert( pixels[0][47][0] < pixels[0][48][0] );

    /* With FXAA before it and RCAS after, into a texture of the surface's
     * size between them. */
    draw_corner_picture();
    assert( gl_postfx_apply_scaled( &f, fx, W, H, W / 2, H / 2, GL_POSTFX_AA_FXAA, 1, gl_postfx_rcas_strength( 0.8f ),
                                    error, sizeof(error) ) );
    no_error();
    assert( fx->width == W / 2 && fx->height == H / 2 && fx->mid_width == W / 2 && fx->up_width == W && fx->up_height == H );
    read_back();
    assert( pixels[H - 1][W - 1][0] == pixels[H - 1][W - 1][2] && pixels[H - 1][W - 1][1] == pixels[H - 1][W - 1][0] );
    /* A picture of the surface's size is not enlarged: with nothing else to
     * do, nothing is drawn. */
    assert( !gl_postfx_apply_scaled( &f, fx, W, H, W, H, GL_POSTFX_AA_OFF, 1, 0, error, sizeof(error) ) );
    assert( !gl_postfx_apply_scaled( &f, fx, W, H, W / 2, H / 2, GL_POSTFX_AA_OFF, 0, 0, error, sizeof(error) ) );
}

/* What a game leaves set at its swap comes back as it was. */
static void test_state( struct gl_postfx *fx )
{
    unsigned int texture, framebuffer, sampler, array, program;
    int viewport[4], value;
    unsigned char mask[4];
    char error[512];

    f.GenTextures( 1, &texture );
    f.ActiveTexture( GLPFX_TEXTURE0 );
    f.BindTexture( GLPFX_TEXTURE_2D, texture );
    f.TexImage2D( GLPFX_TEXTURE_2D, 0, GLPFX_RGBA8, 4, 4, 0, GLPFX_RGBA, GLPFX_UNSIGNED_BYTE, NULL );
    glGenSamplers_( 1, &sampler );
    f.BindSampler( 0, sampler );
    f.ActiveTexture( GLPFX_TEXTURE0 + 3 );
    f.GenFramebuffers( 1, &framebuffer );
    f.BindFramebuffer( GLPFX_READ_FRAMEBUFFER, framebuffer );
    f.FramebufferTexture2D( GLPFX_READ_FRAMEBUFFER, GLPFX_COLOR_ATTACHMENT0, GLPFX_TEXTURE_2D, texture, 0 );
    f.GenVertexArrays( 1, &array );
    f.BindVertexArray( array );
    program = gl_postfx_program( &f, gl_postfx_rcas, error, sizeof(error) );
    f.UseProgram( program );
    f.Viewport( 3, 5, 40, 30 );
    f.ColorMask( 1, 0, 1, 0 );
    f.Enable( GLPFX_BLEND );
    f.Enable( GLPFX_DEPTH_TEST );
    f.Enable( GLPFX_SCISSOR_TEST );
    f.Enable( GLPFX_CULL_FACE );
    f.ReadBuffer( GLPFX_COLOR_ATTACHMENT0 );
    /* The game's own error, waiting for it to ask: left there. */
    f.Enable( 0xffff );

    assert( gl_postfx_apply( &f, fx, W, H, GL_POSTFX_AA_FXAA, gl_postfx_rcas_strength( 0.4f ), error, sizeof(error) ) );
    assert( f.GetError() == 0x0500 /* GL_INVALID_ENUM */ );
    no_error();

    f.GetIntegerv( GLPFX_READ_FRAMEBUFFER_BINDING, &value ); assert( value == (int)framebuffer );
    f.GetIntegerv( GLPFX_READ_BUFFER, &value ); assert( value == GLPFX_COLOR_ATTACHMENT0 );
    f.GetIntegerv( GLPFX_DRAW_FRAMEBUFFER_BINDING, &value ); assert( value == 0 );
    f.GetIntegerv( GLPFX_VERTEX_ARRAY_BINDING, &value ); assert( value == (int)array );
    f.GetIntegerv( GLPFX_CURRENT_PROGRAM, &value ); assert( value == (int)program );
    f.GetIntegerv( GLPFX_ACTIVE_TEXTURE, &value ); assert( value == GLPFX_TEXTURE0 + 3 );
    f.ActiveTexture( GLPFX_TEXTURE0 );
    f.GetIntegerv( GLPFX_TEXTURE_BINDING_2D, &value ); assert( value == (int)texture );
    f.GetIntegerv( GLPFX_SAMPLER_BINDING, &value ); assert( value == (int)sampler );
    f.GetIntegerv( GLPFX_VIEWPORT, viewport );
    assert( viewport[0] == 3 && viewport[1] == 5 && viewport[2] == 40 && viewport[3] == 30 );
    f.GetBooleanv( GLPFX_COLOR_WRITEMASK, mask );
    assert( mask[0] && !mask[1] && mask[2] && !mask[3] );
    assert( f.IsEnabled( GLPFX_BLEND ) && f.IsEnabled( GLPFX_DEPTH_TEST ) && f.IsEnabled( GLPFX_SCISSOR_TEST ) &&
            f.IsEnabled( GLPFX_CULL_FACE ) && !f.IsEnabled( GLPFX_STENCIL_TEST ) );
    no_error();

    /* Back to a plain state for what follows. */
    f.BindFramebuffer( GLPFX_READ_FRAMEBUFFER, 0 );
    f.UseProgram( 0 );
    f.BindVertexArray( 0 );
    f.BindSampler( 0, 0 );
    f.BindTexture( GLPFX_TEXTURE_2D, 0 );
    f.Viewport( 0, 0, W, H );
    f.ColorMask( 1, 1, 1, 1 );
    f.Disable( GLPFX_BLEND );
    f.Disable( GLPFX_DEPTH_TEST );
    f.Disable( GLPFX_SCISSOR_TEST );
    f.Disable( GLPFX_CULL_FACE );
}

/* The program deletes names it never made, as some games purge everything:
 * the passes make theirs again. A new size takes new textures. */
static void test_names_and_sizes( struct gl_postfx *fx )
{
    char error[512];
    unsigned int names[64];

    for (unsigned int i = 0; i < 64; i++) names[i] = i + 1;
    void (*glDeleteTextures_)( int, const unsigned int * ) = get( "glDeleteTextures" );
    void (*glDeleteFramebuffers_)( int, const unsigned int * ) = get( "glDeleteFramebuffers" );
    glDeleteTextures_( 64, names );
    glDeleteFramebuffers_( 64, names );
    f.DeleteProgram( fx->fxaa );
    draw_edge();
    assert( gl_postfx_apply( &f, fx, W, H, GL_POSTFX_AA_FXAA, 0, error, sizeof(error) ) );
    no_error();
    read_back();
    assert( grey_pixels() > 20 );
    draw_edge();
    assert( gl_postfx_apply( &f, fx, W / 2, H / 2, GL_POSTFX_AA_FXAA, gl_postfx_rcas_strength( 0.6f ), error,
                             sizeof(error) ) );
    assert( fx->width == W / 2 && fx->height == H / 2 );
    no_error();
}

int main( void )
{
    static const EGLint config_attribs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                             EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                             EGL_NONE };
    static const EGLint surface_attribs[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_display = (void *)eglGetProcAddress( "eglGetPlatformDisplayEXT" );
    EGLDisplay display = get_display ? get_display( EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL )
                                     : EGL_NO_DISPLAY;
    struct gl_postfx fx = {0};
    EGLConfig config;
    EGLint count = 0;
    EGLSurface surface;
    EGLContext context;

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (display == EGL_NO_DISPLAY || !eglInitialize( display, NULL, NULL ) || !eglBindAPI( EGL_OPENGL_API ) ||
        !eglChooseConfig( display, config_attribs, &config, 1, &count ) || !count)
    {
        printf( "gl_postfx: no OpenGL driver on this host, nothing run\n" );
        return 0;
    }
    surface = eglCreatePbufferSurface( display, config, surface_attribs );
    context = eglCreateContext( display, config, EGL_NO_CONTEXT, NULL );  /* compatibility, as old games have */
    assert( surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT );
    assert( eglMakeCurrent( display, surface, surface, context ) );
    printf( "gl_postfx: %s, %s\n", (const char *)((const unsigned char *(*)(unsigned int))get( "glGetString" ))( 0x1F01 ),
            (const char *)((const unsigned char *(*)(unsigned int))get( "glGetString" ))( 0x1F02 ) );

    assert( gl_postfx_load( &f, get ) );
    glClearColor_ = get( "glClearColor" );
    glClear_ = get( "glClear" );
    glReadPixels_ = get( "glReadPixels" );
    glBegin_ = get( "glBegin" );
    glEnd_ = get( "glEnd" );
    glVertex2f_ = get( "glVertex2f" );
    glColor3f_ = get( "glColor3f" );
    glScissor_ = get( "glScissor" );
    glGenSamplers_ = get( "glGenSamplers" );
    glFinish_ = get( "glFinish" );

    test_fxaa( &fx );
    test_rcas( &fx );
    test_easu( &fx );
    test_state( &fx );
    test_names_and_sizes( &fx );
    glFinish_();
    printf( "gl_postfx: FXAA, EASU, RCAS, the program's state and errors, names it deleted and a new size passed\n" );
    return 0;
}
