/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * The OpenGL calls a program can be sitting in, numbered so that the code that
 * makes the call (win32u's unix side) and the code that counts it (the Switch
 * display driver) agree on the names. A program stuck in one of them and a
 * program retrying one of them forever both look like an idle program from
 * outside; only the name and the rate say which.
 */
#ifndef WINE_NX_GL_ENTRIES_H
#define WINE_NX_GL_ENTRIES_H

enum nx_gl_entry
{
    NX_GL_SET_PIXEL_FORMAT,
    NX_GL_CHOOSE_PIXEL_FORMAT,
    NX_GL_CREATE_CONTEXT,
    NX_GL_MAKE_CURRENT,
    NX_GL_SWAP_BUFFERS,
    NX_GL_ENTRIES
};

void wine_nx_gl_enter( unsigned int entry, int a, int b );
void wine_nx_gl_leave( unsigned int entry, int ret );
/* The Windows-side swap has several calls before it reaches the EGL drawable.
 * These bounded breadcrumbs say which one is the last to return. */
enum nx_gl_swap_stage
{
    NX_GL_SWAP_STAGE_BEGIN,
    NX_GL_SWAP_STAGE_WINDOW,
    NX_GL_SWAP_STAGE_BEFORE_SYNC,
    NX_GL_SWAP_STAGE_AFTER_SYNC,
    NX_GL_SWAP_STAGE_AFTER_FLUSH,
    NX_GL_SWAP_STAGE_BEFORE_EGL,
    NX_GL_SWAP_STAGE_AFTER_EGL,
    NX_GL_SWAP_STAGE_RETURN
};
void wine_nx_gl_swap_stage( unsigned int stage );
int wine_nx_gl_stage_of( unsigned int tid );
const char *wine_nx_gl_entry_name( int entry );

#endif
