/*
 * OpenGL for the Switch display driver
 *
 * Copyright 2026 Wine-NX contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#ifdef __SWITCH__

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "win32u_private.h"
#include "wine/opengl_driver.h"
#include "wine/debug.h"
#include "../../horizon-wine/source/osk.h"
#include "../../horizon-wine/source/controls_overlay.h"
#include "../../horizon-wine/source/graphics_config.h"
#include "../../horizon-wine/source/thread_profile.h"
#include "../../horizon-wine/source/gl_entries.h"

WINE_DEFAULT_DEBUG_CHANNEL(wgl);

/* The runtime hands the screen's NWindow to one OpenGL surface at a time
 * (horizon-wine/source/runtime.c). */
extern void *wine_nx_gl_acquire_window( int width, int height );
extern void wine_nx_gl_release_window( void );
extern int wine_nx_vulkan_owns_screen( void ) __attribute__((weak));
extern void wine_nx_present_fit( int src_w, int src_h, int src_x, int src_y,
                                 int *dst_x, int *dst_y, int *dst_w, int *dst_h ) __attribute__((weak));
extern void wine_nx_present_fit_stretched( int src_w, int src_h, int src_x, int src_y ) __attribute__((weak));
extern void wine_nx_desktop_size( int *width, int *height ) __attribute__((weak));
extern void wine_nx_runtime_trace( const char *msg ) __attribute__((weak));
extern int wine_nx_screenshot_requested( void ) __attribute__((weak));
extern int wine_nx_screenshot_take( const void *pixels, int width, int height, int stride_px, int format ) __attribute__((weak));

static const struct egl_platform *egl;
static const struct opengl_funcs *funcs;
static const struct opengl_drawable_funcs nx_drawable_funcs;

struct nx_gl_drawable
{
    struct opengl_drawable base;
    BOOL screen;  /* this drawable shares the screen's EGL surface */
    LONG generation;
};

struct nx_client_surface
{
    struct client_surface base;
    SIZE size;
    LONG generation;
};

static void nx_client_surface_destroy( struct client_surface *client )
{
}

/* The size of the picture the program draws: the window's own, except a window
 * that covers the whole screen takes the screen's size.  An exclusive game
 * draws its display mode's picture 1:1 at the origin whatever its window is
 * left at, and a window larger than the mode it set would hold that picture in
 * its corner while the crop shows the empty middle of the buffer. */
static SIZE nx_picture_size( struct client_surface *client, const RECT *rect )
{
    SIZE size = { max( 1, rect->right - rect->left ), max( 1, rect->bottom - rect->top ) };
    int width = 0, height = 0;

    if (&wine_nx_desktop_size) wine_nx_desktop_size( &width, &height );
    if (width > 0 && height > 0 && rect->left <= 0 && rect->top <= 0 &&
        size.cx >= width && size.cy >= height)
        size = (SIZE){ width, height };
    return size;
}

static void nx_client_surface_update( struct client_surface *client )
{
    struct nx_client_surface *surface = CONTAINING_RECORD( client, struct nx_client_surface, base );
    const RECT *rect = client->raw ? &client->monitor_rect : &client->virtual_rect;
    SIZE size;
    BOOL offscreen, changed;

    if (!client->format) return; /* Vulkan clients have no WGL pixel format. */
    size = nx_picture_size( client, rect );
    offscreen = !(get_window_long( client->hwnd, GWL_STYLE ) & WS_VISIBLE);
    if (!offscreen && client->hwnd != client->toplevel) offscreen = !is_window_visible( client->hwnd );
    changed = InterlockedExchange( &client->offscreen, offscreen ) != offscreen;

    /* A size change remakes the drawable: a visible window that takes a
     * display mode of its own would otherwise keep a surface of the old size
     * and draw the new picture into its corner. */
    if (changed || size.cx != surface->size.cx || size.cy != surface->size.cy)
        InterlockedIncrement( &surface->generation );
    surface->size = size;
}

static void nx_client_surface_present( struct client_surface *client, HDC hdc )
{
}

static const struct client_surface_funcs nx_client_surface_funcs =
{
    .size = sizeof(struct nx_client_surface),
    .destroy = nx_client_surface_destroy,
    .detach = nx_client_surface_destroy,
    .update = nx_client_surface_update,
    .present = nx_client_surface_present,
};

struct client_surface *wine_nx_drv_CreateClientSurface( HWND hwnd, int format, BOOL raw )
{
    struct client_surface *client = client_surface_create( &nx_client_surface_funcs, hwnd, format, raw );

    if (client) nx_client_surface_update( client );
    return client;
}

/* The one EGL surface on the screen's NWindow, and the drawables sharing it.
 * win32u gives a window a second drawable whenever another context is made
 * current on it while the first is still in use - wined3d does exactly that
 * when it replaces its caps context with a versioned one - so the drawables
 * of a window share the surface rather than each asking for the screen. */
static pthread_mutex_t nx_screen_mutex = PTHREAD_MUTEX_INITIALIZER;
static EGLSurface nx_screen_surface;
static unsigned int nx_screen_refs;
static int nx_screen_format;

static struct nx_gl_drawable *impl_from_opengl_drawable( struct opengl_drawable *base )
{
    return CONTAINING_RECORD( base, struct nx_gl_drawable, base );
}

BOOL wine_nx_gl_drawable_matches_window( struct opengl_drawable *base )
{
    struct nx_client_surface *client;

    if (base->funcs != &nx_drawable_funcs) return TRUE;
    if (!base->client || base->client->funcs != &nx_client_surface_funcs) return TRUE;
    /* Measure the window now: a display mode of the program's own changes the
     * picture's size without the window moving, and nothing else would raise
     * the generation the drawable is kept or remade by. */
    client_surface_update( base->client );
    client = CONTAINING_RECORD( base->client, struct nx_client_surface, base );
    return impl_from_opengl_drawable( base )->generation ==
           InterlockedCompareExchange( &client->generation, 0, 0 );
}

static void nx_log( const char *format, ... )
{
    char buffer[256];
    va_list args;

    if (!&wine_nx_runtime_trace) return;
    va_start( args, format );
    vsnprintf( buffer, sizeof(buffer), format, args );
    va_end( args );
    wine_nx_runtime_trace( buffer );
}

/* What the program's context is, for the stall report: a game that never swaps
 * a frame is one whose OpenGL is either unusable or unused, and the version
 * says which at a glance -- a context that is not there at all reads
 * differently from one reported as 4.3. */
static char nx_program_gl[80] = "no context yet";

const char *wine_nx_gl_program_version( void )
{
    return nx_program_gl;
}

/* What the program's own context turned out to be, said once, the first time it
 * draws or the first time it is made current: the GL and GLSL versions it was
 * given, whether that is a core or a compatibility profile, and how long the
 * extension list it reads is. All four are what a game of the 2000s quietly
 * depends on: GLSL 1.20 shaders do not compile in a core profile at all, and
 * the extension list is what such a game copies into a buffer of its era's
 * size -- the launcher's year switch is what shortens that list, and this line
 * is how a change to it is seen. */
static void nx_note_program_gl( void )
{
    const char *version, *shading, *extensions;
    GLint profile = -1, count = 0;
    static int noted;

    if (InterlockedExchange( &noted, 1 ) || !funcs->p_glGetString) return;
    version = (const char *)funcs->p_glGetString( GL_VERSION );
    if (version) snprintf( nx_program_gl, sizeof(nx_program_gl), "%s", version );
    shading = (const char *)funcs->p_glGetString( GL_SHADING_LANGUAGE_VERSION );
    extensions = (const char *)funcs->p_glGetString( GL_EXTENSIONS );
    if (funcs->p_glGetIntegerv) funcs->p_glGetIntegerv( GL_CONTEXT_PROFILE_MASK, &profile );
    if (!extensions)
    {
        /* A core profile has no list to read: the extensions are numbered. */
        if (funcs->p_glGetIntegerv) funcs->p_glGetIntegerv( GL_NUM_EXTENSIONS, &count );
        nx_log( "[NXGL] the program's OpenGL: %s (GLSL %s, %s profile), %d extensions by index",
                version ? version : "?", shading ? shading : "?",
                profile == GL_CONTEXT_CORE_PROFILE_BIT ? "a core" : "a compatibility", (int)count );
        return;
    }
    nx_log( "[NXGL] the program's OpenGL: %s (GLSL %s, %s profile), its extension list is %u bytes",
            version ? version : "?", shading ? shading : "?",
            profile == GL_CONTEXT_CORE_PROFILE_BIT ? "a core" : "a compatibility",
            (unsigned int)strlen( extensions ) );
}

/* The shape of the context the program asked for, before the driver answers:
 * a legacy program asks for a compatibility profile and no version, and what
 * it gets back is the driver's own default. */
static void nx_note_context_request( int format, const EGLint *attribs, const EGLint *end )
{
    const char *profile = "the driver's default profile";
    int major = 0, minor = 0;

    for (; attribs != end; attribs += 2)
    {
        switch (attribs[0])
        {
        case EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR:
            if (attribs[1] & EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR) profile = "a compatibility profile";
            else if (attribs[1] & EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR) profile = "a core profile";
            break;
        case EGL_CONTEXT_MAJOR_VERSION_KHR:
            major = attribs[1];
            break;
        case EGL_CONTEXT_MINOR_VERSION_KHR:
            minor = attribs[1];
            break;
        }
    }
    if (major) nx_log( "[NXGL] the program asks for OpenGL %d.%d, %s (format %d)", major, minor, profile, format );
    else nx_log( "[NXGL] the program asks for %s (format %d)", profile, format );
}

/* Drops one attribute from a context attribute list, the terminator included.
 * FALSE if the list does not have it. */
static BOOL nx_drop_context_attribute( EGLint *attribs, EGLint **end, EGLint name )
{
    EGLint *dst;

    for (dst = attribs; dst != *end && dst[0] != name; dst += 2) continue;
    if (dst == *end) return FALSE;
    memmove( dst, dst + 2, (size_t)( *end - dst - 1 ) * sizeof(*dst) );
    *end -= 2;
    **end = EGL_NONE;
    return TRUE;
}

/* A program can be inside one of the calls below for as long as the driver
 * takes, and one that retries a call forever -- which is what Dark Athena turned
 * out to be doing with wglSetPixelFormat -- looks from the outside exactly like
 * an idle one: no frames, one core, no line in the log. Each call is counted per
 * thread, with the arguments of its first calls and of each doubling, and the
 * call a thread is inside is kept where the stall report can read it, because
 * nothing else in a log says which of them is stuck. */
static const char *const nx_gl_entry_names[NX_GL_ENTRIES] =
{
    "wglSetPixelFormat", "wglChoosePixelFormat", "wglCreateContext", "wglMakeCurrent", "wglSwapBuffers",
};

/* Per thread: how often each was called, and what the last one returned. */
static __thread unsigned int nx_gl_calls[NX_GL_ENTRIES];
static __thread int nx_gl_last_ret[NX_GL_ENTRIES];
static __thread int nx_gl_inside = -1;

/* Which call each thread is inside now, for the stall report: a thread cannot
 * read another's thread-local storage, so the few words it needs are here.
 * The thread id is stored one above itself, so that a slot nobody has filled
 * -- which reads as tid 0, entry 0 -- does not answer "thread 0 is inside
 * wglSetPixelFormat".  Thread 0 is a real entry in the stall report, and it
 * spent two reports being named after a call it had not made. */
struct nx_gl_stage { unsigned int tid; int entry; };
#define NX_GL_STAGES 64
static struct nx_gl_stage nx_gl_stages[NX_GL_STAGES];

static unsigned int nx_gl_current_tid( void )
{
    return (unsigned int)HandleToULong( NtCurrentTeb()->ClientId.UniqueThread );
}

void wine_nx_gl_enter( unsigned int entry, int a, int b )
{
    struct nx_gl_stage *slot;
    unsigned int tid = nx_gl_current_tid(), count;

    if (entry >= NX_GL_ENTRIES || !&wine_nx_runtime_trace) return;
    nx_gl_inside = (int)entry;
    slot = &nx_gl_stages[tid % NX_GL_STAGES];
    __atomic_store_n( &slot->entry, (int)entry, __ATOMIC_RELAXED );
    __atomic_store_n( &slot->tid, tid + 1, __ATOMIC_RELAXED );

    count = ++nx_gl_calls[entry];
    if (!nx_prof_counted_line( count )) return;
    nx_log( "[NXGL] %s(%d, %d) called %u times; the one before returned %d", nx_gl_entry_names[entry],
            a, b, count, nx_gl_last_ret[entry] );
}

void wine_nx_gl_leave( unsigned int entry, int ret )
{
    struct nx_gl_stage *slot;
    unsigned int tid;

    if (entry >= NX_GL_ENTRIES) return;
    nx_gl_last_ret[entry] = ret;
    if (nx_gl_inside != (int)entry) return;
    nx_gl_inside = -1;
    tid = nx_gl_current_tid();
    slot = &nx_gl_stages[tid % NX_GL_STAGES];
    if (__atomic_load_n( &slot->tid, __ATOMIC_RELAXED ) == tid + 1)
        __atomic_store_n( &slot->entry, -1, __ATOMIC_RELAXED );
}

/* For the stall report: which call this thread is inside, or -1. */
int wine_nx_gl_stage_of( unsigned int tid )
{
    struct nx_gl_stage *slot = &nx_gl_stages[tid % NX_GL_STAGES];

    if (__atomic_load_n( &slot->tid, __ATOMIC_RELAXED ) != tid + 1) return -1;
    return __atomic_load_n( &slot->entry, __ATOMIC_RELAXED );
}

const char *wine_nx_gl_entry_name( int entry )
{
    return entry >= 0 && entry < NX_GL_ENTRIES ? nx_gl_entry_names[entry] : NULL;
}

/* The EGL config of a pixel format, as win32u's egldrv chooses it for pbuffers. */
static EGLConfig nx_config_for_format( int format )
{
    return egl->configs[(format - 1) % egl->config_count];
}

static void nx_drawable_destroy( struct opengl_drawable *base )
{
    struct nx_gl_drawable *gl = impl_from_opengl_drawable( base );
    EGLSurface surface = NULL;

    if (!gl->screen)
    {
        /* win32u destroys the EGL surface after this callback. */
        return;
    }

    /* The screen's surface outlives this drawable while another one shares it;
     * take it away from win32u either way, so the framebuffer only gets the
     * NWindow back once the surface is really gone. */
    base->surface = NULL;

    pthread_mutex_lock( &nx_screen_mutex );
    if (nx_screen_refs && !--nx_screen_refs)
    {
        surface = nx_screen_surface;
        nx_screen_surface = NULL;
    }
    pthread_mutex_unlock( &nx_screen_mutex );

    if (!surface) return;
    funcs->p_eglDestroySurface( egl->display, surface );
    wine_nx_gl_release_window();
}

static void nx_drawable_flush( struct opengl_drawable *base, UINT flags )
{
    TRACE( "drawable %s, flags %#x\n", debugstr_opengl_drawable( base ), flags );

    if (impl_from_opengl_drawable( base )->screen && (flags & GL_FLUSH_INTERVAL))
        funcs->p_eglSwapInterval( egl->display, abs( base->interval ) );
}

/* Frames presented and the time inside eglSwapBuffers, for [PROGRESS]. */
extern unsigned long long horizon_interrupt_time(void);
unsigned int wine_nx_gl_swaps;
unsigned long long wine_nx_gl_swap_time;  /* 100 ns */
/* When the swap the program is inside now began, 0 while none is. A swap that
 * never comes back can be the whole of a program's run -- Dark Athena called
 * wglSwapBuffers once and sat inside it at 100 % of a core for the rest of the
 * two minutes it was given -- and from outside that looks the same as a program
 * that never reached its first frame. The first three swaps are named with
 * their surface's size and what they returned, and the runtime's stall report
 * reads the start back (runtime.c, stall_watch) to say a swap is stuck and for
 * how long. */
static unsigned long long nx_swap_begun_at;
static unsigned int nx_swap_named;

/* Milliseconds the program's swap has been inside the driver, 0 if none is.
 * Read from another thread, hence the atomics; the clock is the same one the
 * frame times are measured with. */
unsigned int wine_nx_gl_swap_stuck_ms( void )
{
    unsigned long long began = __atomic_load_n( &nx_swap_begun_at, __ATOMIC_ACQUIRE ), now;

    if (!began) return 0;
    now = horizon_interrupt_time();
    if (now <= began) return 0;
    return (unsigned int)((now - began) / 10000);  /* 100 ns ticks to ms */
}

/* The floating keyboard (horizon-wine/source/osk.c), put into the back
 * buffer just before it is shown: its picture in a texture of each context's
 * own, blitted from a framebuffer of its own onto the window's. Everything
 * the blit and the upload depend on is put back as the program left it.
 * The controls overlay draws through the same calls, so this is whichever
 * overlay is up. */
struct nx_osk_gl
{
    EGLContext context;
    GLuint texture, framebuffer;
    int width, height;
    unsigned int generation;
    void *pixels;
};
static struct nx_osk_gl nx_osk_gl[8];
static unsigned int nx_osk_gl_next;
static PFN_glGenFramebuffers p_glGenFramebuffers;
static PFN_glBindFramebuffer p_glBindFramebuffer;
static PFN_glFramebufferTexture2D p_glFramebufferTexture2D;
static PFN_glBlitFramebuffer p_glBlitFramebuffer;
static PFN_glIsFramebuffer p_glIsFramebuffer;
static PFN_glBindBuffer p_glBindBuffer;

static void nx_osk_draw( struct opengl_drawable *base )
{
    struct wine_nx_osk_frame frame;
    struct nx_osk_gl *osk = NULL;
    EGLint width = 0, height = 0;
    EGLContext context;
    GLint read_fb, draw_fb, texture, unpack_buffer, row_length, skip_pixels, skip_rows, alignment;
    GLboolean scissor, srgb;
    unsigned int i;

    if (!wine_nx_osk_visible() && !wine_nx_controls_overlay_visible()) return;
    if (!funcs->p_eglQuerySurface( egl->display, base->surface, EGL_WIDTH, &width ) ||
        !funcs->p_eglQuerySurface( egl->display, base->surface, EGL_HEIGHT, &height ))
        return;
    /* The picture is laid out for the 1280x720 panel -- the screen its taps
     * are read against (controls_overlay.c) -- not for this surface: the
     * surface is the panel's picture scaled to the window, a 4:3 game's
     * 800x600 stretched widescreen among them, and drawing the layout at the
     * surface's own size made the overlay a stretched, cropped thing that no
     * tap lined up with. The blit below maps the panel's rectangle into the
     * surface's, so it lands on the panel as it was drawn. */
    if (!wine_nx_osk_frame( 1280, 720, &frame ) || !(context = funcs->p_eglGetCurrentContext()))
        return;
    if (!p_glBlitFramebuffer)
    {
        p_glGenFramebuffers = (void *)funcs->p_eglGetProcAddress( "glGenFramebuffers" );
        p_glBindFramebuffer = (void *)funcs->p_eglGetProcAddress( "glBindFramebuffer" );
        p_glFramebufferTexture2D = (void *)funcs->p_eglGetProcAddress( "glFramebufferTexture2D" );
        p_glIsFramebuffer = (void *)funcs->p_eglGetProcAddress( "glIsFramebuffer" );
        p_glBindBuffer = (void *)funcs->p_eglGetProcAddress( "glBindBuffer" );
        if (!p_glGenFramebuffers || !p_glBindFramebuffer || !p_glFramebufferTexture2D || !p_glIsFramebuffer ||
            !p_glBindBuffer)
            return;
        p_glBlitFramebuffer = (void *)funcs->p_eglGetProcAddress( "glBlitFramebuffer" );
        if (!p_glBlitFramebuffer) return;
    }
    for (i = 0; i < ARRAY_SIZE(nx_osk_gl) && !osk; i++)
        if (nx_osk_gl[i].context == context) osk = &nx_osk_gl[i];
    if (!osk)
    {
        /* A context not seen before: its names are its own, so the slot's
         * old ones are only forgotten, never deleted from here. */
        osk = &nx_osk_gl[nx_osk_gl_next++ % ARRAY_SIZE(nx_osk_gl)];
        free( osk->pixels );
        memset( osk, 0, sizeof(*osk) );
        osk->context = context;
    }

    funcs->p_glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &read_fb );
    funcs->p_glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &draw_fb );
    funcs->p_glGetIntegerv( GL_TEXTURE_BINDING_2D, &texture );
    funcs->p_glGetIntegerv( GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer );
    funcs->p_glGetIntegerv( GL_UNPACK_ROW_LENGTH, &row_length );
    funcs->p_glGetIntegerv( GL_UNPACK_SKIP_PIXELS, &skip_pixels );
    funcs->p_glGetIntegerv( GL_UNPACK_SKIP_ROWS, &skip_rows );
    funcs->p_glGetIntegerv( GL_UNPACK_ALIGNMENT, &alignment );
    scissor = funcs->p_glIsEnabled( GL_SCISSOR_TEST );
    srgb = funcs->p_glIsEnabled( GL_FRAMEBUFFER_SRGB );

    /* Names the program deleted, or a context that took an old one's place. */
    if (!osk->texture || !funcs->p_glIsTexture( osk->texture ) || !p_glIsFramebuffer( osk->framebuffer ))
    {
        funcs->p_glGenTextures( 1, &osk->texture );
        p_glGenFramebuffers( 1, &osk->framebuffer );
        osk->width = osk->height = 0;
    }
    funcs->p_glBindTexture( GL_TEXTURE_2D, osk->texture );
    p_glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
    funcs->p_glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
    funcs->p_glPixelStorei( GL_UNPACK_SKIP_PIXELS, 0 );
    funcs->p_glPixelStorei( GL_UNPACK_SKIP_ROWS, 0 );
    funcs->p_glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
    if (osk->width != frame.width || osk->height != frame.height)
    {
        free( osk->pixels );
        osk->pixels = malloc( (size_t)frame.width * frame.height * 4 );
        funcs->p_glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, frame.width, frame.height, 0, GL_BGRA,
                               GL_UNSIGNED_BYTE, NULL );
        funcs->p_glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
        funcs->p_glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
        osk->width = frame.width;
        osk->height = frame.height;
        osk->generation = 0;
    }
    if (osk->pixels && osk->generation != frame.generation &&
        (osk->generation = wine_nx_osk_copy( 1280, 720, osk->pixels, frame.width * 4, 0 )))
        funcs->p_glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height, GL_BGRA, GL_UNSIGNED_BYTE,
                                  osk->pixels );
    if (osk->generation)
    {
        p_glBindFramebuffer( GL_READ_FRAMEBUFFER, osk->framebuffer );
        p_glFramebufferTexture2D( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, osk->texture, 0 );
        p_glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
        if (scissor) funcs->p_glDisable( GL_SCISSOR_TEST );
        if (srgb) funcs->p_glDisable( GL_FRAMEBUFFER_SRGB );
        /* The picture's first row is its top; the window's first is its
         * bottom. Panel pixels, mapped into the surface's own. */
        {
            int dst_x0 = (int)((long long)frame.x * width / 1280);
            int dst_y0 = (int)((long long)frame.y * height / 720);
            int dst_x1 = (int)((long long)(frame.x + frame.width) * width / 1280);
            int dst_y1 = (int)((long long)(frame.y + frame.height) * height / 720);

            p_glBlitFramebuffer( 0, 0, frame.width, frame.height, dst_x0, height - dst_y0,
                                 dst_x1, height - dst_y1, GL_COLOR_BUFFER_BIT, GL_NEAREST );
        }
        if (scissor) funcs->p_glEnable( GL_SCISSOR_TEST );
        if (srgb) funcs->p_glEnable( GL_FRAMEBUFFER_SRGB );
    }

    p_glBindFramebuffer( GL_READ_FRAMEBUFFER, read_fb );
    p_glBindFramebuffer( GL_DRAW_FRAMEBUFFER, draw_fb );
    funcs->p_glBindTexture( GL_TEXTURE_2D, texture );
    p_glBindBuffer( GL_PIXEL_UNPACK_BUFFER, unpack_buffer );
    funcs->p_glPixelStorei( GL_UNPACK_ROW_LENGTH, row_length );
    funcs->p_glPixelStorei( GL_UNPACK_SKIP_PIXELS, skip_pixels );
    funcs->p_glPixelStorei( GL_UNPACK_SKIP_ROWS, skip_rows );
    funcs->p_glPixelStorei( GL_UNPACK_ALIGNMENT, alignment );
}

/* The frame about to be shown, kept when the player asked for it (Minus+R;
 * horizon-wine/source/runtime.c).  glReadPixels comes up bottom-up, which the
 * sink is told.  The read buffer is taken back to whatever the program left
 * bound. */
static void nx_drawable_screenshot( struct opengl_drawable *base )
{
    GLint width = 0, height = 0, read_fb = 0;
    void *pixels;

    if (!&wine_nx_screenshot_requested || !wine_nx_screenshot_requested()) return;
    if (!&wine_nx_screenshot_take || !funcs->p_glReadPixels || !p_glBindFramebuffer) return;
    if (!funcs->p_eglQuerySurface( egl->display, base->surface, EGL_WIDTH, &width ) ||
        !funcs->p_eglQuerySurface( egl->display, base->surface, EGL_HEIGHT, &height ) ||
        width < 1 || height < 1) return;
    if (!(pixels = malloc( (size_t)width * height * 4 ))) return;
    funcs->p_glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &read_fb );
    p_glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
    funcs->p_glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels );
    p_glBindFramebuffer( GL_READ_FRAMEBUFFER, read_fb );
    wine_nx_screenshot_take( pixels, width, height, width * 4, WINE_NX_SHOT_RGBA8_BOTTOM_UP );
    free( pixels );
}

static BOOL nx_drawable_swap( struct opengl_drawable *base )
{
    unsigned long long start;
    EGLint width = 0, height = 0;
    unsigned int index;
    BOOL ret;

    nx_note_program_gl();
    if (!impl_from_opengl_drawable( base )->screen)
    {
        /* A drawable without the screen is a pbuffer: there is no window to
         * present to, and the driver refuses eglSwapBuffers on one. Saying the
         * swap failed would send a program round a retry loop over something
         * that is not a failure, so the flush the caller already did stands. */
        return TRUE;
    }
    if (&wine_nx_vulkan_owns_screen && wine_nx_vulkan_owns_screen())
    {
        /* A Vulkan surface has the screen and shows it; an OpenGL surface left
         * holding on (a ddraw proxy's wined3d, a GDI paint) must not queue
         * buffers behind the picture. */
        static int stood_down;

        if (!stood_down)
        {
            stood_down = 1;
            nx_log( "[NXGL] OpenGL swaps stand down; a Vulkan surface shows the screen" );
        }
        return TRUE;
    }
    /* The drawable's own buffer, which the display's scaler stretches over the
     * panel: input maps through that stretch, not through the Scaling setting
     * the Vulkan blit and the compositor follow. */
    if (&wine_nx_present_fit_stretched && base->client)
    {
        if (funcs->p_eglQuerySurface( egl->display, base->surface, EGL_WIDTH, &width ) &&
            funcs->p_eglQuerySurface( egl->display, base->surface, EGL_HEIGHT, &height ))
            wine_nx_present_fit_stretched( width, height, base->client->virtual_rect.left,
                                           base->client->virtual_rect.top );
    }
    nx_osk_draw( base );
    nx_drawable_screenshot( base );
    /* The first three swaps say what they are, because the first one is where
     * a program that shows nothing stops: a swap the driver never returns used
     * to leave the log with no line between the context and the stall report,
     * and the window it was swapped on was the question. */
    index = __atomic_add_fetch( &nx_swap_named, 1, __ATOMIC_RELAXED );
    if (index <= 3)
        nx_log( "[NXGL] swap #%u began on the program's %dx%d surface", index, width, height );
    start = horizon_interrupt_time();
    /* The first one in, not the last: two windows can swap at once, and the
     * one worth naming is the one that has been in there the longest. */
    if (!__atomic_load_n( &nx_swap_begun_at, __ATOMIC_ACQUIRE ))
        __atomic_store_n( &nx_swap_begun_at, start, __ATOMIC_RELEASE );
    ret = funcs->p_eglSwapBuffers( egl->display, base->surface );
    if (__atomic_load_n( &nx_swap_begun_at, __ATOMIC_RELAXED ) == start)
        __atomic_store_n( &nx_swap_begun_at, 0, __ATOMIC_RELEASE );
    __atomic_add_fetch( &wine_nx_gl_swap_time, horizon_interrupt_time() - start, __ATOMIC_RELAXED );
    __atomic_add_fetch( &wine_nx_gl_swaps, 1, __ATOMIC_RELAXED );
    if (index <= 3)
    {
        if (ret)
            nx_log( "[NXGL] swap #%u came back after %u ms", index,
                    (unsigned int)((horizon_interrupt_time() - start) / 10000) );
        else
            nx_log( "[NXGL] swap #%u failed after %u ms: EGL error 0x%x", index,
                    (unsigned int)((horizon_interrupt_time() - start) / 10000),
                    funcs->p_eglGetError() );
    }
    return ret;
}

static const struct opengl_drawable_funcs nx_drawable_funcs =
{
    .size = sizeof(struct nx_gl_drawable),
    .destroy = nx_drawable_destroy,
    .flush = nx_drawable_flush,
    .swap = nx_drawable_swap,
};

/* A window that cannot have the screen still gets a surface to draw into:
 * offscreen, shown nowhere, the way a hidden window's is. Failing the call
 * instead leaves a program with no context at all -- and one that treats a
 * failed wglSetPixelFormat as "ask again", which Dark Athena does, never stops
 * asking: no frames, one core, and a log with nothing in it. */
static BOOL nx_offscreen_surface( struct nx_gl_drawable *gl, struct client_surface *client, int format )
{
    SIZE size = client->raw ? gl->base.monitor_size : gl->base.virtual_size;
    EGLint attribs[5], error;

    if (size.cx < 1) size.cx = 640;
    if (size.cy < 1) size.cy = 480;
    attribs[0] = EGL_WIDTH;
    attribs[1] = size.cx;
    attribs[2] = EGL_HEIGHT;
    attribs[3] = size.cy;
    attribs[4] = EGL_NONE;

    if (!(gl->base.surface = funcs->p_eglCreatePbufferSurface( egl->display, nx_config_for_format( format ),
                                                              attribs )))
    {
        error = funcs->p_eglGetError();
        nx_log( "[NXGL] hwnd %p has no surface in format %d: eglCreatePbufferSurface failed, error %#x",
                client->hwnd, format, error );
        return FALSE;
    }
    gl->screen = FALSE;
    return TRUE;
}

/* Visible OpenGL windows share the screen; hidden windows render offscreen. */
static BOOL nx_surface_create( struct client_surface *client, int format, struct opengl_drawable **drawable )
{
    HWND hwnd = client->hwnd;
    struct opengl_drawable *previous;
    struct nx_gl_drawable *gl;
    struct nx_client_surface *nx_client = CONTAINING_RECORD( client, struct nx_client_surface, base );
    LONG generation = client->funcs == &nx_client_surface_funcs ?
                      InterlockedCompareExchange( &nx_client->generation, 0, 0 ) : 0;
    void *window;

    TRACE( "hwnd %p, format %d\n", hwnd, format );

    if ((previous = *drawable) && previous->format == format && wine_nx_gl_drawable_matches_window( previous ))
        return TRUE;
    /* A previous surface may hold the screen: let it go first. */
    if (previous)
    {
        opengl_drawable_release( previous );
        *drawable = NULL;
    }

    gl = opengl_drawable_create( &nx_drawable_funcs, format, client, NULL );
    if (!gl) return FALSE;
    gl->generation = generation;
    gl->base.buffer_map[0] = GL_BACK_LEFT;
    gl->base.buffer_map[1] = GL_BACK_RIGHT;
    gl->base.buffer_map[GL_FRONT - GL_FRONT_LEFT] = GL_BACK;
    gl->base.buffer_map[GL_FRONT_AND_BACK - GL_FRONT_LEFT] = GL_BACK;

    /* A hidden window, and a capability probe that must not take the screen away
     * from Vulkan, both draw offscreen. */
    if (InterlockedCompareExchange( &client->offscreen, 0, 0 ))
    {
        if (!nx_offscreen_surface( gl, client, format )) goto err;
        TRACE( "hwnd %p: offscreen surface %p\n", hwnd, gl->base.surface );
        *drawable = &gl->base;
        return TRUE;
    }

    pthread_mutex_lock( &nx_screen_mutex );
    if (nx_screen_surface && nx_screen_format == format)
    {
        /* Another drawable of this window still has the screen; share it. */
        gl->base.surface = nx_screen_surface;
        gl->screen = TRUE;
        nx_screen_refs++;
        pthread_mutex_unlock( &nx_screen_mutex );
        TRACE( "hwnd %p: sharing the screen surface %p\n", hwnd, nx_screen_surface );
        *drawable = &gl->base;
        return TRUE;
    }
    if (nx_screen_surface)
    {
        static unsigned int fell_back;
        unsigned int count;
        int taken = nx_screen_format;

        pthread_mutex_unlock( &nx_screen_mutex );
        /* The screen belongs to a surface of another format: a second window
         * surface of a different config cannot be made on the same NWindow.
         * The window draws offscreen instead of failing, so the program keeps
         * its context and its frame loop. */
        count = __atomic_add_fetch( &fell_back, 1, __ATOMIC_RELAXED );
        if (nx_prof_counted_line( count ))
            nx_log( "[NXGL] hwnd %p asks for format %d and the screen's surface is format %d; "
                    "this window draws offscreen (%u so far)", hwnd, format, taken, count );
        if (!nx_offscreen_surface( gl, client, format )) goto err;
        *drawable = &gl->base;
        return TRUE;
    }
    pthread_mutex_unlock( &nx_screen_mutex );

    {
        SIZE size = nx_picture_size( client, client->raw ? &client->monitor_rect : &client->virtual_rect );

        if (!(window = wine_nx_gl_acquire_window( size.cx, size.cy )))
        {
            /* Someone else -- a Vulkan surface, the compositor's own presenter --
             * holds the screen. Draw offscreen rather than leave the program
             * without a context. */
            nx_log( "[NXGL] hwnd %p cannot have the screen (another surface holds it); it draws offscreen", hwnd );
            if (!nx_offscreen_surface( gl, client, format )) goto err;
            *drawable = &gl->base;
            return TRUE;
        }
    }
    gl->screen = TRUE;
    if (!(gl->base.surface = funcs->p_eglCreateWindowSurface( egl->display, nx_config_for_format( format ),
                                                              (EGLNativeWindowType)window, NULL )))
    {
        ERR( "hwnd %p: eglCreateWindowSurface failed, error %#x\n", hwnd, funcs->p_eglGetError() );
        /* nothing is sharing the screen yet, so give it back here */
        gl->screen = FALSE;
        wine_nx_gl_release_window();
        goto err;
    }

    pthread_mutex_lock( &nx_screen_mutex );
    nx_screen_surface = gl->base.surface;
    nx_screen_format = format;
    nx_screen_refs = 1;
    pthread_mutex_unlock( &nx_screen_mutex );

    TRACE( "created drawable %s with EGL surface %p\n", debugstr_opengl_drawable( &gl->base ), gl->base.surface );
    {
        static BOOL logged;
        const char *vendor = funcs->p_eglQueryString( egl->display, EGL_VENDOR );
        const char *version = funcs->p_eglQueryString( egl->display, EGL_VERSION );

        /* raw 0: win32u draws through its framebuffer surface (DPI scaling or gamma) */
        if (!logged) nx_log( "[NXGL] EGL %s %s, %u configs; window surface for format %d, raw %u",
                             vendor ? vendor : "?", version ? version : "?", egl->config_count, format, client->raw );
        logged = TRUE;
    }
    *drawable = &gl->base;
    return TRUE;

err:
    opengl_drawable_release( &gl->base );
    return FALSE;
}

/* win32u's egldrv creates contexts without a config, but switch-mesa 20.1's EGL
 * driver lacks EGL_KHR_no_config_context (such a context fails with
 * EGL_BAD_CONFIG) and EGL_KHR_create_context_no_error. A context gets the config
 * of its pixel format, the one this format's window surfaces and pbuffers use,
 * so EGL lets them be made current together. */
static struct opengl_context *nx_context_create( int format, struct opengl_context *share,
                                                  const int *attribs, BOOL *shared )
{
    EGLint egl_attribs[16], *end = egl_attribs, error;
    struct opengl_context *context;

    TRACE( "format %d, share %p, attribs %p\n", format, share, attribs );

    for (; attribs && attribs[0]; attribs += 2)
    {
        EGLint name, *dst;

        switch (attribs[0])
        {
        case WGL_CONTEXT_MAJOR_VERSION_ARB:
            name = EGL_CONTEXT_MAJOR_VERSION_KHR;
            break;
        case WGL_CONTEXT_MINOR_VERSION_ARB:
            name = EGL_CONTEXT_MINOR_VERSION_KHR;
            break;
        case WGL_CONTEXT_FLAGS_ARB:
            name = EGL_CONTEXT_FLAGS_KHR;
            break;
        case WGL_CONTEXT_PROFILE_MASK_ARB:
            if (attribs[1] & WGL_CONTEXT_ES2_PROFILE_BIT_EXT)
            {
                ERR( "OpenGL ES contexts are not supported\n" );
                return NULL;
            }
            name = EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR;
            break;
        case WGL_CONTEXT_OPENGL_NO_ERROR_ARB:
            FIXME( "no-error contexts are unavailable, ignoring %#x\n", attribs[1] );
            continue;
        default:
            FIXME( "unhandled attribute %#x %#x\n", attribs[0], attribs[1] );
            continue;
        }

        /* A repeated attribute replaces the earlier one. */
        for (dst = egl_attribs; dst != end && *dst != name; dst += 2) continue;
        if (dst == end)
        {
            if (end - egl_attribs >= (int)ARRAY_SIZE(egl_attribs) - 2) continue;
            end += 2;
        }
        dst[0] = name;
        dst[1] = attribs[1];
    }
    *end = EGL_NONE;
    nx_note_context_request( format, egl_attribs, end );

    if (!(context = calloc( 1, sizeof(*context) ))) return NULL;
    funcs->p_eglBindAPI( EGL_OPENGL_API );
    context->host_context = funcs->p_eglCreateContext( egl->display, nx_config_for_format( format ),
                                                        share ? share->host_context : NULL,
                                                        end != egl_attribs ? egl_attribs : NULL );
    error = funcs->p_eglGetError();

    /* A driver built around the core profile can refuse the compatibility one
     * that wglCreateContext asks for, and then a game of the 2000s has no
     * context at all and paints nothing. Letting the profile go -- and, if the
     * version is refused too, that as well -- leaves the driver its own
     * default, which is the plain context such a program was written against. */
    if (error != EGL_SUCCESS || !context->host_context)
    {
        static const EGLint droppable[] = { EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_MAJOR_VERSION_KHR };
        unsigned int i;

        for (i = 0; i < ARRAY_SIZE(droppable) && (error != EGL_SUCCESS || !context->host_context); i++)
        {
            if (!nx_drop_context_attribute( egl_attribs, &end, droppable[i] )) continue;
            if (droppable[i] == EGL_CONTEXT_MAJOR_VERSION_KHR)
            {
                nx_drop_context_attribute( egl_attribs, &end, EGL_CONTEXT_MINOR_VERSION_KHR );
                nx_drop_context_attribute( egl_attribs, &end, EGL_CONTEXT_FLAGS_KHR );
            }
            nx_log( "[NXGL] the driver refused a context with %#x, retrying without it", droppable[i] );
            context->host_context = funcs->p_eglCreateContext( egl->display, nx_config_for_format( format ),
                                                                share ? share->host_context : NULL,
                                                                end != egl_attribs ? egl_attribs : NULL );
            error = funcs->p_eglGetError();
        }
    }
    if (error != EGL_SUCCESS || !context->host_context)
    {
        ERR( "context creation failed for format %d, share %p, error %#x\n", format, share, error );
        free( context );
        return NULL;
    }
    TRACE( "created context %p\n", context );
    return context;
}

/* The driver's own way of making a context current, kept so ours can say what
 * happened: a refusal here is what a program sees as "no rendering at all",
 * and eglMakeCurrent's answer is the only place it is visible. */
static BOOL (*nx_base_context_activate)( struct opengl_context *, struct opengl_drawable *, struct opengl_drawable * );

static BOOL nx_context_activate( struct opengl_context *context, struct opengl_drawable *draw,
                                 struct opengl_drawable *read )
{
    BOOL ret = nx_base_context_activate( context, draw, read );
    EGLint error = funcs->p_eglGetError();

    if (!ret)
    {
        static int failures;

        if (failures++ < 4)
            nx_log( "[NXGL] the program's OpenGL context could not be made current, error %#x", error );
        return FALSE;
    }
    /* Only the program's own window tells us what it got; a context that draws
     * into a pbuffer or the compositor's own is none of our business. */
    if (draw && draw->surface == nx_screen_surface) nx_note_program_gl();
    return TRUE;
}

/* Mesa's Switch platform is the default display; its windows are NWindows. */
static void nx_init_egl_platform( struct egl_platform *platform )
{
    platform->type = 0;
    platform->native_display = 0;
    egl = platform;
}

static struct opengl_driver_funcs nx_driver_funcs =
{
    .p_init_egl_platform = nx_init_egl_platform,
    .p_surface_create = nx_surface_create,
    .p_context_create = nx_context_create,
    .p_context_activate = nx_context_activate,
};

UINT wine_nx_drv_OpenGLInit( UINT version, const struct opengl_funcs *opengl_funcs,
                             const struct opengl_driver_funcs **driver_funcs )
{
    if (version != WINE_OPENGL_DRIVER_VERSION)
    {
        ERR( "version mismatch, opengl32 wants %u but the driver has %u\n", version, WINE_OPENGL_DRIVER_VERSION );
        return STATUS_INVALID_PARAMETER;
    }
    if (!opengl_funcs->egl_handle) return STATUS_NOT_SUPPORTED;
    funcs = opengl_funcs;

    nx_driver_funcs.p_get_proc_address = (*driver_funcs)->p_get_proc_address;
    nx_driver_funcs.p_init_pixel_formats = (*driver_funcs)->p_init_pixel_formats;
    nx_driver_funcs.p_describe_pixel_format = (*driver_funcs)->p_describe_pixel_format;
    nx_driver_funcs.p_init_extensions = (*driver_funcs)->p_init_extensions;
    nx_driver_funcs.p_context_destroy = (*driver_funcs)->p_context_destroy;
    nx_base_context_activate = (*driver_funcs)->p_context_activate;
    nx_driver_funcs.p_pbuffer_create = (*driver_funcs)->p_pbuffer_create;
    nx_driver_funcs.p_pbuffer_updated = (*driver_funcs)->p_pbuffer_updated;
    nx_driver_funcs.p_pbuffer_bind = (*driver_funcs)->p_pbuffer_bind;
    nx_driver_funcs.p_null_surface_create = (*driver_funcs)->p_null_surface_create;
    nx_driver_funcs.p_cleanup_thread = (*driver_funcs)->p_cleanup_thread;

    *driver_funcs = &nx_driver_funcs;
    return STATUS_SUCCESS;
}

#endif /* __SWITCH__ */
