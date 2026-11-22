/*
 *    Copyright (c) 2000 Lionel Ulmer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
#ifndef __WINE_OPENGL32_PRIVATE_H
#define __WINE_OPENGL32_PRIVATE_H

#include <stdarg.h>
#include <stddef.h>

#include "ntstatus.h"
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "wingdi.h"
#include "wine/opengl_driver.h"
#include "thunks.h"

struct registry_entry
{
    const char *name;      /* name of the extension */
    void *func;
    UINT16 major;
    UINT16 minor;
    enum opengl_extension extensions[4];
};

extern int WINAPI wglDescribePixelFormat( HDC hdc, int ipfd, UINT cjpfd, PIXELFORMATDESCRIPTOR *ppfd );
extern BOOL get_pbuffer_from_handle( HPBUFFERARB handle, HPBUFFERARB *obj );
extern BOOL get_context_from_handle( HGLRC handle, HGLRC *obj );
extern BOOL get_sync_from_handle( GLsync handle, GLsync *obj );
extern void set_gl_error( GLenum error );
extern struct registry_entry *get_function_entry( const char *name );
extern BOOL get_integer( GLenum pname, GLuint index, GLint value, GLint *data );
extern void set_context_attribute( GLenum name, const void *value, size_t size );

enum object_type
{
    OBJ_TYPE_BUFFER,
    OBJ_TYPE_DISPLAY_LIST,
    OBJ_TYPE_FRAMEBUFFER,
    OBJ_TYPE_MEMORY,
    OBJ_TYPE_PATH,
    OBJ_TYPE_PROGRAM,
    OBJ_TYPE_RENDERBUFFER,
    OBJ_TYPE_SAMPLER,
    OBJ_TYPE_SEMAPHORE,
    OBJ_TYPE_SHADER,
    OBJ_TYPE_SHADER_ATI,
    OBJ_TYPE_SHADER_EXT,
    OBJ_TYPE_TEXTURE,
    OBJ_TYPE_COUNT,
};

static inline GLuint *memdup_objects( UINT n, const GLuint *handles, GLuint *buf, UINT max )
{
    GLuint *tmp = buf;
    if (!handles || (n > max && !(tmp = malloc( n * sizeof(*handles) )))) return NULL;
    memcpy( tmp, handles, n * sizeof(*handles) );
    return tmp;
}

extern void put_context_objects( enum object_type type, UINT n, GLuint *handles );
extern GLuint put_context_object_range( enum object_type type, UINT n, GLuint base );
extern BOOL alloc_context_objects( enum object_type type, UINT n, const GLuint *handles, BOOL extension );
extern GLuint *del_context_objects( enum object_type type, UINT n, GLuint *handles );
extern GLuint *map_context_objects( enum object_type type, UINT n, GLuint *handles );
extern BOOL alloc_map_context_object( enum object_type type, GLuint *handle, BOOL extension );

/* A 32-bit program's calls that only set the current context's state are kept
 * in a batch of its thread's (make_opengl, %deferred_calls) and cross the
 * WoW64 unix boundary together, which for a game such as Doom 3 is some 25000
 * crossings a frame fewer: each is FEX's bridge, the runtime's unix call
 * dispatcher and its kernel state, in translated and native code. Any other
 * call sends the batch first, so that the host runs them in the program's
 * order (UNIX_CALL), and so does the end of the thread. A 64-bit program's
 * calls, and every call with WINE_NX_GL_BATCH=0, cross one by one. */
#include "unixlib.h"

/* The thread's batch, in the TEB beside the slots wgl.c uses. */
#define WINE_GL_RESERVED_CALL_BATCH 7

#ifndef _WIN64

struct call_batch
{
    unsigned int used;      /* bytes of data in use */
    unsigned int flushing;  /* the batch is being run: a call made meanwhile crosses at once */
    unsigned char data[0x4000];
};

extern void flush_call_batch( struct call_batch *batch );
extern NTSTATUS defer_call( enum unix_funcs func, void *params, unsigned int size,
                            const void *const *field, unsigned int data_size );

static inline void flush_calls(void)
{
    struct call_batch *batch = NtCurrentTeb()->glReserved1[WINE_GL_RESERVED_CALL_BATCH];
    if (batch && batch->used) flush_call_batch( batch );
}

#undef UNIX_CALL
#define UNIX_CALL( func, params ) (flush_calls(), WINE_UNIX_CALL( unix_ ## func, params ))
#define UNIX_DEFER( func, params ) defer_call( unix_ ## func, params, sizeof(*(params)), NULL, 0 )
/* With an array the call reads, which goes into the batch: field is the
 * parameter pointing at it, size its size in bytes. */
#define UNIX_DEFER_DATA( func, params, field, size ) \
    defer_call( unix_ ## func, params, sizeof(*(params)), (const void *const *)&(params)->field, size )

#else

static inline void flush_calls(void) {}
#define UNIX_DEFER( func, params ) UNIX_CALL( func, params )
#define UNIX_DEFER_DATA( func, params, field, size ) UNIX_CALL( func, params )

#endif

#endif /* __WINE_OPENGL32_PRIVATE_H */
