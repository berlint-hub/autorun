#!/usr/bin/env python3
"""The arrow the OpenGL overlay puts into a program's picture: which runs it
blits where, that the sprite and the framebuffer are made once per context, and
that the program's GL state is what it was afterwards.

The code is taken from dlls/win32u/winnx_opengl.c as it is and runs against
mocks: the sandbox has no EGL or OpenGL, so this is the only way the overlay is
compiled and run outside the Switch. The Vulkan one (dlls/win32u/vulkan.c) is
checked by tests/check_cursor_vulkan.py.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
nx = (root / "dlls/win32u/winnx_opengl.c").read_text()


def between(source, start, end):
    assert start in source and end in source, start
    return source[source.index(start):source.index(end)]


fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "pointer_overlay.h"

typedef int BOOL, GLint, GLsizei, EGLint;
typedef unsigned int GLuint, GLenum, GLbitfield;
typedef unsigned char GLboolean;
typedef void *EGLContext, *EGLSurface, *EGLDisplay;
#define FALSE 0
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define GL_TEXTURE_2D 0x0DE1
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_SCISSOR_TEST 0x0C11
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_RGBA8 0x8058
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_NEAREST 0x2600
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define EGL_WIDTH 1
#define EGL_HEIGHT 2

struct opengl_drawable { EGLSurface surface; };
struct egl_platform { void *display; };
static const struct egl_platform platform = { .display = (void *)1 };
static const struct egl_platform *egl = &platform;

/* The table the overlay calls through (winnx_opengl.c declares it before the
 * keyboard's overlay and fills it from eglGetProcAddress). */
struct opengl_funcs
{
    int (*p_eglQuerySurface)(EGLDisplay, EGLSurface, EGLint, EGLint *);
    EGLContext (*p_eglGetCurrentContext)(void);
    void *(*p_eglGetProcAddress)(const char *);
    void (*p_glGenTextures)(GLsizei, GLuint *);
    GLboolean (*p_glIsTexture)(GLuint);
    void (*p_glBindTexture)(GLenum, GLuint);
    void (*p_glTexParameteri)(GLenum, GLenum, GLint);
    void (*p_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
    void (*p_glPixelStorei)(GLenum, GLint);
    void (*p_glGetIntegerv)(GLenum, GLint *);
    GLboolean (*p_glIsEnabled)(GLenum);
    void (*p_glEnable)(GLenum);
    void (*p_glDisable)(GLenum);
};
static struct opengl_funcs *funcs;
/* Weak in winnx_opengl.c: a runtime that has no arrow, or no log to write to,
 * must leave the overlay doing nothing rather than not link. */
extern int wine_nx_cursor_panel( int *x, int *y ) __attribute__((weak));
extern void wine_nx_runtime_trace( const char *msg ) __attribute__((weak));
extern int wine_nx_surface_picture( int surface_w, int surface_h, int *x, int *y, int *w, int *h )
    __attribute__((weak));
/* Static in winnx_opengl.c, which this fixture takes only a slice of, so it is
 * declared here the way the runtime's own hooks are. */
extern void nx_log( const char *format, ... ) __attribute__((weak));

typedef void (*PFN_glGenFramebuffers)(GLint, GLuint *);
typedef void (*PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void (*PFN_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (*PFN_glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef GLboolean (*PFN_glIsFramebuffer)(GLuint);
typedef void (*PFN_glBindBuffer)(GLenum, GLuint);
'''

implementation = (between(nx, "static PFN_glGenFramebuffers", "static void nx_osk_draw(") +
                  between(nx, "struct nx_cursor_gl\n{", "/* The frame about to be shown, kept when"))

mocks = r'''
/* What the program left behind -- as the overlay finds it, and as it must leave
 * it -- and what the overlay did to it. */
static struct
{
    GLint read_fb, draw_fb, texture, unpack_buffer, row_length, skip_pixels, skip_rows, alignment;
    GLboolean scissor, srgb;
    GLuint bound_texture, bound_buffer;
    unsigned int uploads, copies, attaches, framebuffer_binds, texture_binds, buffer_binds;
    GLint copy_src[POINTER_OVERLAY_MAX_RUNS][4], copy_dst[POINTER_OVERLAY_MAX_RUNS][4];
} program =
{
    .read_fb = 3, .draw_fb = 4, .texture = 11, .unpack_buffer = 12,
    .row_length = 64, .skip_pixels = 2, .skip_rows = 3, .alignment = 1,
    .scissor = 1, .srgb = 1,
};
static const EGLSurface mock_surface = (EGLSurface *)0x5555;
static EGLint surface_width = 1280, surface_height = 720;
static int cursor_panel_x = 100, cursor_panel_y = 50, cursor_visible = 1;

static int mock_query_surface( EGLDisplay display, EGLSurface surface, EGLint attribute, EGLint *value )
{
    if (surface != mock_surface) return 0;
    if (attribute == EGL_WIDTH) *value = surface_width;
    else if (attribute == EGL_HEIGHT) *value = surface_height;
    else return 0;
    return 1;
}
/* Which context is current is set by the checks: the overlay keeps the arrow's
 * names per context, so a test of two of them has to be able to switch. */
static EGLContext mock_context = (EGLContext)0x1234;
static EGLContext mock_current_context( void ) { return mock_context; }
static void *mock_get_proc_address( const char *name );

static void mock_gen_textures( GLsizei count, GLuint *textures )
{
    assert(count == 1);
    *textures = 7;   /* a name the program is not using */
}
static GLboolean mock_is_texture( GLuint texture ) { return texture == 7; }
static void mock_bind_texture( GLenum target, GLuint texture )
{
    assert(target == GL_TEXTURE_2D);
    program.bound_texture = texture;
    program.texture_binds++;
}
static void mock_tex_parameter( GLenum target, GLenum name, GLint value ) { assert(value == GL_NEAREST); }
static void mock_tex_image( GLenum target, GLint level, GLint internal, GLsizei width, GLsizei height,
                            GLint border, GLenum format, GLenum type, const void *pixels )
{
    const uint32_t *sprite = pixels;
    int i;

    assert(target == GL_TEXTURE_2D && level == 0 && internal == GL_RGBA8);
    assert(width == POINTER_CURSOR_W && height == POINTER_CURSOR_H && !border);
    assert(format == GL_RGBA && type == GL_UNSIGNED_BYTE);
    assert(pixels);
    /* The sprite the compositor draws: opaque black or white, clear elsewhere,
     * its tip at the sprite's top-left. */
    for (i = 0; i < POINTER_CURSOR_W * POINTER_CURSOR_H; i++)
    {
        assert(sprite[i] == 0xffffffffu || sprite[i] == 0xff000000u || sprite[i] == 0u);
        if (pointer_cursor_shape[i / POINTER_CURSOR_W][i % POINTER_CURSOR_W] == '.')
            assert(sprite[i] == 0u);
    }
    assert(sprite[0] == 0xff000000u);
    program.uploads++;
}
static void mock_pixel_store( GLenum what, GLint value )
{
    switch (what)
    {
    case GL_UNPACK_ROW_LENGTH: program.row_length = value; break;
    case GL_UNPACK_SKIP_PIXELS: program.skip_pixels = value; break;
    case GL_UNPACK_SKIP_ROWS: program.skip_rows = value; break;
    case GL_UNPACK_ALIGNMENT: program.alignment = value; break;
    default: assert(0);
    }
}
static void mock_get_integer( GLenum what, GLint *value )
{
    switch (what)
    {
    case GL_READ_FRAMEBUFFER_BINDING: *value = program.read_fb; break;
    case GL_DRAW_FRAMEBUFFER_BINDING: *value = program.draw_fb; break;
    case GL_TEXTURE_BINDING_2D: *value = program.texture; break;
    case GL_PIXEL_UNPACK_BUFFER_BINDING: *value = program.unpack_buffer; break;
    case GL_UNPACK_ROW_LENGTH: *value = program.row_length; break;
    case GL_UNPACK_SKIP_PIXELS: *value = program.skip_pixels; break;
    case GL_UNPACK_SKIP_ROWS: *value = program.skip_rows; break;
    case GL_UNPACK_ALIGNMENT: *value = program.alignment; break;
    default: assert(0);
    }
}
static GLboolean mock_is_enabled( GLenum what ) { return what == GL_SCISSOR_TEST ? program.scissor : program.srgb; }
static void mock_enable( GLenum what )
{
    if (what == GL_SCISSOR_TEST) program.scissor = 1;
    else { assert(what == GL_FRAMEBUFFER_SRGB); program.srgb = 1; }
}
static void mock_disable( GLenum what )
{
    if (what == GL_SCISSOR_TEST) program.scissor = 0;
    else { assert(what == GL_FRAMEBUFFER_SRGB); program.srgb = 0; }
}

static struct opengl_funcs mock_functions =
{
    mock_query_surface, mock_current_context, mock_get_proc_address, mock_gen_textures, mock_is_texture,
    mock_bind_texture, mock_tex_parameter, mock_tex_image, mock_pixel_store, mock_get_integer,
    mock_is_enabled, mock_enable, mock_disable,
};

static void mock_gen_framebuffers( GLint count, GLuint *framebuffers )
{
    assert(count == 1);
    *framebuffers = 9;   /* a name the program is not using */
}
static void mock_bind_framebuffer( GLenum target, GLuint framebuffer )
{
    if (target == GL_READ_FRAMEBUFFER) program.read_fb = framebuffer;
    else program.draw_fb = framebuffer;
    program.framebuffer_binds++;
}
static void mock_framebuffer_texture( GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level )
{
    assert(target == GL_READ_FRAMEBUFFER && attachment == GL_COLOR_ATTACHMENT0);
    assert(textarget == GL_TEXTURE_2D && texture == 7 && level == 0);
    program.attaches++;
}
static void mock_blit( GLint src_x0, GLint src_y0, GLint src_x1, GLint src_y1,
                       GLint dst_x0, GLint dst_y0, GLint dst_x1, GLint dst_y1, GLbitfield mask, GLenum filter )
{
    unsigned int i = program.copies;

    assert(mask == GL_COLOR_BUFFER_BIT && filter == GL_NEAREST);
    assert(i < POINTER_OVERLAY_MAX_RUNS);
    program.copy_src[i][0] = src_x0; program.copy_src[i][1] = src_y0;
    program.copy_src[i][2] = src_x1; program.copy_src[i][3] = src_y1;
    program.copy_dst[i][0] = dst_x0; program.copy_dst[i][1] = dst_y0;
    program.copy_dst[i][2] = dst_x1; program.copy_dst[i][3] = dst_y1;
    program.copies++;
}
static GLboolean mock_is_framebuffer( GLuint framebuffer ) { return framebuffer == 9; }
static void mock_bind_buffer( GLenum target, GLuint buffer )
{
    assert(target == GL_PIXEL_UNPACK_BUFFER);
    program.bound_buffer = buffer;
    program.buffer_binds++;
}
static void *mock_get_proc_address( const char *name )
{
    if (!strcmp( name, "glGenFramebuffers" )) return mock_gen_framebuffers;
    if (!strcmp( name, "glBindFramebuffer" )) return mock_bind_framebuffer;
    if (!strcmp( name, "glFramebufferTexture2D" )) return mock_framebuffer_texture;
    if (!strcmp( name, "glBlitFramebuffer" )) return mock_blit;
    if (!strcmp( name, "glIsFramebuffer" )) return mock_is_framebuffer;
    if (!strcmp( name, "glBindBuffer" )) return mock_bind_buffer;
    return NULL;
}
static char trace_line[256];
static unsigned int traced;

void wine_nx_runtime_trace( const char *message )
{
    traced++;
    snprintf( trace_line, sizeof(trace_line), "%s", message );
}

static char overlay_log_line[256];
static unsigned int overlay_logged;

/* The overlay says what it did into the runtime's log, and in the sandbox there
 * is no runtime to write one: those lines are what a program frozen on its first
 * swap is read out of, so they are kept and the checks ask for them. */
void nx_log( const char *format, ... )
{
    va_list args;

    overlay_logged++;
    va_start( args, format );
    vsnprintf( overlay_log_line, sizeof(overlay_log_line), format, args );
    va_end( args );
}

int wine_nx_cursor_panel( int *x, int *y )
{
    if (!cursor_visible) return 0;
    *x = cursor_panel_x;
    *y = cursor_panel_y;
    return 1;
}

/* Which part of the surface the panel shows: all of it, unless the test says
 * the display is cropping it (the widescreen hack's central 4:3). */
static int shown_x, shown_y, shown_w, shown_h;
int wine_nx_surface_picture( int surface_w, int surface_h, int *x, int *y, int *w, int *h )
{
    *x = shown_x;
    *y = shown_y;
    *w = shown_w ? shown_w : surface_w;
    *h = shown_h ? shown_h : surface_h;
    return 1;
}
'''

checks = r'''
static void check_runs( int px, int py, int width, int height )
{
    struct pointer_overlay_run runs[POINTER_OVERLAY_MAX_RUNS];
    int count = pointer_overlay_runs( px, py, 1280, 720, shown_x, shown_y,
                                      shown_w ? shown_w : width, shown_h ? shown_h : height, runs ), i;

    assert(count > 0 && (unsigned int)count == program.copies);
    for (i = 0; i < count; i++)
    {
        const struct pointer_overlay_run *run = &runs[i];

        /* A sprite row is a row of the arrow from its top, the framebuffer's
         * first row is its bottom. */
        assert(program.copy_src[i][0] == run->src_x0);
        assert(program.copy_src[i][1] == POINTER_CURSOR_H - run->src_y - 1);
        assert(program.copy_src[i][2] == run->src_x1);
        assert(program.copy_src[i][3] == POINTER_CURSOR_H - run->src_y);
        assert(program.copy_dst[i][0] == run->x0);
        assert(program.copy_dst[i][1] == height - run->y1);
        assert(program.copy_dst[i][2] == run->x1);
        assert(program.copy_dst[i][3] == height - run->y0);
        /* Nothing of the arrow is copied onto the surface's edge, and inside
         * the part of it the panel shows. */
        assert(run->x0 >= shown_x && run->x1 <= shown_x + (shown_w ? shown_w : width));
        assert(run->y0 >= shown_y && run->y1 <= shown_y + (shown_h ? shown_h : height));
        assert(run->x0 < run->x1 && run->y0 < run->y1);
    }
}

static void check_state_restored( void )
{
    assert(program.read_fb == 3 && program.draw_fb == 4);
    assert(program.texture == 11 && program.bound_texture == 11);
    assert(program.unpack_buffer == 12 && program.bound_buffer == 12);
    assert(program.row_length == 64 && program.skip_pixels == 2 && program.skip_rows == 3 && program.alignment == 1);
    assert(program.scissor && program.srgb);
}

int main(void)
{
    struct opengl_drawable drawable = {0};
    unsigned int uploads_before;

    funcs = &mock_functions;
    drawable.surface = mock_surface;

    /* The program hid the cursor: the picture is left alone. */
    cursor_visible = 0;
    nx_cursor_draw( &drawable );
    assert(!program.copies && !program.uploads && !program.framebuffer_binds);

    /* The panel's own size: every opaque row of the arrow, where the compositor
     * would have put it, sprite and framebuffer made once. */
    cursor_visible = 1;
    nx_cursor_draw( &drawable );
    assert(program.uploads == 1 && program.copies > 0);
    check_runs( cursor_panel_x, cursor_panel_y, surface_width, surface_height );
    check_state_restored();

    program.copies = 0;
    program.framebuffer_binds = program.attaches = 0;
    nx_cursor_draw( &drawable );
    assert(program.uploads == 1 && program.attaches == 0);
    /* Said once, the first frame it drew: the log can then tell a swap that
     * never got here from one that did. */
    assert(traced == 1 && strstr( trace_line, "mouse arrow" ));
    /* The arrow's 22 opaque pieces at the panel's own size, on the surface the
     * picture is drawn into. */
    assert(strstr( trace_line, "22 pieces" ) && strstr( trace_line, "1280x720" ));
    check_runs( cursor_panel_x, cursor_panel_y, surface_width, surface_height );

    /* A surface that is the panel's picture at another size -- a 1080p window
     * on a 720p panel: the same arrow, scaled with the picture. */
    program.copies = 0;
    surface_width = 1920; surface_height = 1080;
    nx_cursor_draw( &drawable );
    check_runs( cursor_panel_x, cursor_panel_y, 1920, 1080 );

    /* At the panel's corner only what the picture has is copied. */
    program.copies = 0;
    cursor_panel_x = 1279; cursor_panel_y = 719;
    nx_cursor_draw( &drawable );
    check_runs( 1279, 719, 1920, 1080 );

    /* A window smaller than the panel, and a 4:3 one, where the picture's rows
     * and the arrow's do not line up. */
    program.copies = 0;
    cursor_panel_x = 300; cursor_panel_y = 200;
    surface_width = 640; surface_height = 480;
    nx_cursor_draw( &drawable );
    check_runs( 300, 200, 640, 480 );
    program.copies = 0;
    surface_width = 800; surface_height = 600;
    nx_cursor_draw( &drawable );
    check_runs( 300, 200, 800, 600 );
    check_state_restored();

    /* The widescreen hack: the display shows the central 4:3 of a 1280x720
     * buffer, stretched over the panel.  The arrow belongs on that rectangle,
     * at that rectangle's scale, not on the whole buffer. */
    program.copies = 0;
    surface_width = 1280; surface_height = 720;
    shown_x = 160; shown_w = 960;
    nx_cursor_draw( &drawable );
    check_runs( cursor_panel_x, cursor_panel_y, 1280, 720 );
    check_state_restored();
    /* A panel pixel at the far side is a pixel of the picture, never past it. */
    program.copies = 0;
    cursor_panel_x = 1279;
    nx_cursor_draw( &drawable );
    check_runs( 1279, cursor_panel_y, 1280, 720 );
    cursor_panel_x = 100;
    shown_x = shown_w = 0;

    /* A context that is new to the overlay takes an old one's slot: the names
     * belong to the context that is gone, so they are made again, not used. */
    program.copies = 0;
    surface_width = 1280; surface_height = 720;
    nx_cursor_draw( &drawable );
    check_runs( cursor_panel_x, cursor_panel_y, 1280, 720 );

    /* A context prepared the way activation prepares it, before it ever
     * presents: the names and the sprite are made outside any swap, so the first
     * frame drawn with it uploads nothing again and attaches nothing again --
     * the work that used to fall to the program's first wglSwapBuffers. */
    program.copies = program.framebuffer_binds = program.attaches = 0;
    uploads_before = program.uploads;
    mock_context = (EGLContext)0x5678;
    nx_cursor_prepare( funcs->p_eglGetCurrentContext() );
    assert( program.uploads == uploads_before + 1 && program.attaches == 1 );
    check_state_restored();
    program.copies = program.framebuffer_binds = program.attaches = 0;
    nx_cursor_draw( &drawable );
    assert( program.uploads == uploads_before + 1 && program.attaches == 0 && program.copies > 0 );
    check_runs( cursor_panel_x, cursor_panel_y, surface_width, surface_height );
    check_state_restored();
    mock_context = (EGLContext)0x1234;

    /* The arrow's own words for the draw it made: a run that froze on its first
     * swap is read out of these lines, so they are part of what is checked. */
    assert( overlay_logged > 0 );
    assert( !strcmp( overlay_log_line, "[NXGL] the mouse arrow: the GL state is put back" ) );

    printf( "cursor overlay: the arrow's runs blitted into the picture, once per context\n" );
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="autorun-cursor-overlay-") as tmp:
    tmp = Path(tmp)
    (tmp / "pointer_overlay.h").write_text((root / "horizon-wine/source/pointer_overlay.h").read_text())
    (tmp / "pointer_cursor.h").write_text((root / "horizon-wine/source/pointer_cursor.h").read_text())
    (tmp / "cursor_overlay.c").write_text(fixture + implementation + mocks + checks)
    subprocess.run([os.environ.get("WINE_NX_HOST_CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-Wno-address", "-fsanitize=address,undefined",
                    str(tmp / "cursor_overlay.c"), "-o", str(tmp / "cursor_overlay")], check=True)
    subprocess.run([str(tmp / "cursor_overlay")], check=True)
print("Cursor overlay: runs, scale, edges and one upload per context")
