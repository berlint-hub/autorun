#!/usr/bin/env python3
"""opengl32's batch of a 32-bit program's state-only calls (make_opengl's
%deferred_calls, wgl.c's defer_call, unix_wgl.c's wow64_flush_calls).

The thunks of calls that only set the current context's state keep them in a
batch of the thread's, which crosses the WoW64 unix boundary in one call
before any other call. This checks the generated thunks, and runs the PE side
and the unix side as the sources have them over a recording host: the host
must see every call in the program's order, with the arrays as they were when
the call was made, however the batch fills, with batching off, with an array
missing, with a call made while the batch runs, and a batch that is not one
is refused without reading past it."""
import os
import pathlib
import re
import shutil
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
gl = root / 'dlls/opengl32'
make_opengl = (gl / 'make_opengl').read_text()
thunks = (gl / 'thunks.c').read_text()
unixlib = (gl / 'unixlib.h').read_text()
private = (gl / 'private.h').read_text()
wgl = (gl / 'wgl.c').read_text()
unix_wgl = (gl / 'unix_wgl.c').read_text()
unix_thunks = (gl / 'unix_thunks.c').read_text()

# --- The generated code ------------------------------------------------------
block = make_opengl[make_opengl.index('my %deferred_calls ='):]
block = block[:block.index(');')]
deferred = dict(re.findall(r'"(gl\w+)" => "([^"]*)"', block))
assert len(deferred) >= 40, len(deferred)
for name, data in deferred.items():
    thunk = re.search(r'\n(?:static )?void WINAPI ' + name + r'\(.*?\n}\n', thunks, re.S)
    assert thunk, f'{name}: no void thunk'
    thunk = thunk.group(0)
    assert 'UNIX_CALL(' not in thunk, name
    if data:
        field, size = data.split(':')
        assert f'UNIX_DEFER_DATA( {name}, &args, {field}, {size} )' in thunk, name
        params = re.search(r'struct ' + name + r'_params\n{(.*?)};', unixlib, re.S).group(1)
        assert re.search(r'const \w+ \*' + field + ';', params), (name, field)
    else:
        assert f'UNIX_DEFER( {name}, &args )' in thunk, name
    # Only the parameters and nothing written back: the call returns nothing,
    # and its thunk reads no result after it.
    assert 'args.ret' not in thunk and 'return args' not in thunk, name
assert thunks.count('UNIX_DEFER') == len(deferred)
# Appended, so every other call keeps its number; in both tables.
enum = unixlib[unixlib.index('enum unix_funcs'):]
enum = enum[:enum.index('};')]
assert enum.rstrip().endswith('unix_flush_calls,\n    funcs_count')
assert '    flush_calls,\n};' in unix_thunks and '    wow64_flush_calls,\n};' in unix_thunks
# Every call that is not deferred sends the batch first.
assert '#define UNIX_CALL( func, params ) (flush_calls(), WINE_UNIX_CALL( unix_ ## func, params ))' in private
assert not re.search(r'\bWINE_UNIX_CALL\(', thunks)
assert re.findall(r'WINE_UNIX_CALL\( (\w+)', wgl) == ['unix_flush_calls', 'func']

# --- The PE side and the unix side over a recording host ---------------------
pe = wgl[wgl.index('#ifndef _WIN64\n\n/* WINE_NX_GL_BATCH=0'):]
pe = pe[len('#ifndef _WIN64\n'):pe.index('\n#endif\n')]
header = private[private.index('#define WINE_GL_RESERVED_CALL_BATCH'):private.index('#else\n\nstatic inline void flush_calls(void) {}')]
header = header.replace('#ifndef _WIN64\n', '')
unix = unix_wgl[unix_wgl.index('static const void *batched_call('):]
unix = unix[:unix.index('NTSTATUS wow64_get_pixel_formats(')]

fixture = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
typedef int NTSTATUS; typedef int BOOL; typedef unsigned int UINT32, PTR32, GLenum, GLuint; typedef float GLfloat;
typedef unsigned short WCHAR;
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000d)
#define WARN(...) do {} while (0)
#define ERR(...) do {} while (0)
#define ULongToPtr(u) ((void *)(uintptr_t)(u))
typedef struct { void *glReserved1[29]; } TEB;
static TEB teb;
static TEB *NtCurrentTeb(void) { return &teb; }
enum unix_funcs { unix_glBindTexture, unix_glProgramEnvParameter4fvARB, unix_glGetError, unix_flush_calls, funcs_count };
struct flush_calls_params { TEB *teb; void *data; unsigned int size; };
struct glBindTexture_params { TEB *teb; GLenum target; GLuint texture; };
struct glProgramEnvParameter4fvARB_params { TEB *teb; GLenum target; GLuint index; const GLfloat *params; };
struct glGetError_params { TEB *teb; GLenum ret; };
/* The batch below 4 GB, as a 32-bit program's heap is. */
static void *low_calloc( size_t count, size_t size )
{
    void *p = mmap( NULL, count * size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0 );
    return p == MAP_FAILED ? NULL : p;
}
#define calloc low_calloc
static NTSTATUS host_call( enum unix_funcs func, void *params );
#define WINE_UNIX_CALL( func, params ) host_call( func, params )
#define UNIX_CALL( func, params ) WINE_UNIX_CALL( unix_ ## func, params )
'''

test = r'''
#undef calloc
static const unixlib_entry_t *dummy;
/* What the host ran, in order. */
static char seen[40000];
static int crossings, nested;
static void note( const char *text ) { strcat( seen, text ); strcat( seen, " " ); }

static NTSTATUS bind_texture( void *args )
{
    const struct glBindTexture_params *p = args;
    char text[32];
    snprintf( text, sizeof(text), "bind%u", p->texture );
    note( text );
    /* A call the host makes the program make while the batch runs (as a debug
     * message callback could): it crosses at once, it is not lost. */
    if (p->texture == 100999)
    {
        struct glBindTexture_params again = { &teb, 1, 101000 };
        nested++;
        assert( !UNIX_DEFER( glBindTexture, &again ) );
    }
    return STATUS_SUCCESS;
}
static NTSTATUS env_parameter( void *args )
{
    const struct glProgramEnvParameter4fvARB_params *p = args;
    char text[64];
    if (!p->params) snprintf( text, sizeof(text), "env%u=null", p->index );
    else snprintf( text, sizeof(text), "env%u=%g,%g,%g,%g", p->index, p->params[0], p->params[1], p->params[2], p->params[3] );
    note( text );
    return STATUS_SUCCESS;
}
static NTSTATUS get_error( void *args ) { (void)args; note( "error" ); return STATUS_SUCCESS; }
static NTSTATUS never( void *args ) { (void)args; abort(); }
const unixlib_entry_t __wine_unix_call_wow64_funcs[funcs_count] = { bind_texture, env_parameter, get_error, never };

static NTSTATUS host_call( enum unix_funcs func, void *params )
{
    crossings++;
    if (func == unix_flush_calls)
    {
        /* The 32-bit parameters the unix side reads. */
        const struct flush_calls_params *p = params;
        struct { PTR32 teb; PTR32 data; UINT32 size; } wow64 = { 0, (PTR32)(uintptr_t)p->data, p->size };
        assert( (uintptr_t)p->data < 0x100000000ull );
        return wow64_flush_calls( &wow64 );
    }
    return __wine_unix_call_wow64_funcs[func]( params );
}

static void bind( GLuint texture )
{
    struct glBindTexture_params args = { &teb, 1, texture };
    assert( !UNIX_DEFER( glBindTexture, &args ) );
}
static void env( GLuint index, GLfloat *v )
{
    struct glProgramEnvParameter4fvARB_params args = { &teb, 2, index, v };
    assert( !UNIX_DEFER_DATA( glProgramEnvParameter4fvARB, &args, params, 4 * sizeof(GLfloat) ) );
}
static void error_call(void)
{
    struct glGetError_params args = { &teb, 0 };
    UNIX_CALL( glGetError, &args );
}
static void reset(void) { seen[0] = 0; crossings = 0; }

int main( void )
{
    GLfloat v[4] = { 1, 2, 3, 4 };
    static char expected[40000];
    unsigned int i;
    int full;
    (void)dummy;

    /* Deferred, then a call that is not: all of them cross once, in order, and
     * the array is what it was when the call was made. */
    reset();
    bind( 1 ); env( 7, v ); v[0] = 9; bind( 2 );
    assert( crossings == 0 && !seen[0] );
    error_call();
    assert( !strcmp( seen, "bind1 env7=1,2,3,4 bind2 error " ) && crossings == 2 );

    /* More than a batch holds: it goes when full, and nothing is lost or reordered. */
    reset();
    for (i = 0; i < 3000; i++) bind( i );
    flush_calls();
    expected[0] = 0;
    for (i = 0; i < 3000; i++) { char t[16]; snprintf( t, sizeof(t), "bind%u ", i ); strcat( expected, t ); }
    assert( !strcmp( seen, expected ) && crossings > 3 && crossings < 3000 / 100 );
    full = crossings;

    /* No array where one is read: it crosses at once, after what is batched. */
    reset();
    bind( 5 );
    {
        struct glProgramEnvParameter4fvARB_params args = { &teb, 2, 3, NULL };
        UNIX_DEFER_DATA( glProgramEnvParameter4fvARB, &args, params, 4 * sizeof(GLfloat) );
    }
    assert( !strcmp( seen, "bind5 env3=null " ) && crossings == 2 );

    /* A call made while the batch runs crosses at once and the batch ends empty. */
    reset();
    nested = 0;
    bind( 100998 ); bind( 100999 ); bind( 101001 );
    flush_calls();
    assert( nested == 1 && !strcmp( seen, "bind100998 bind100999 bind101000 bind101001 " ) );
    assert( !((struct call_batch *)teb.glReserved1[WINE_GL_RESERVED_CALL_BATCH])->used );

    /* Batching off (WINE_NX_GL_BATCH=0): each call crosses by itself. */
    munmap( teb.glReserved1[WINE_GL_RESERVED_CALL_BATCH], sizeof(struct call_batch) );
    teb.glReserved1[WINE_GL_RESERVED_CALL_BATCH] = NULL;
    call_batch_disabled = 1;
    reset();
    bind( 1 ); bind( 2 );
    assert( crossings == 2 && !strcmp( seen, "bind1 bind2 " ) && !teb.glReserved1[WINE_GL_RESERVED_CALL_BATCH] );

    /* A batch that is not one is refused, and nothing past it is read. */
    {
        static const unsigned int sizes[][3] = { { 0, 4, 16 }, { 0, 12, 16 }, { 0, 24, 16 }, { 3, 16, 16 }, { 9, 16, 16 }, { 0, 16, 12 } };
        unsigned int k;
        for (k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++)
        {
            unsigned int *bad = low_calloc( 1, 4096 );
            struct { PTR32 teb; PTR32 data; UINT32 size; } wow64 = { 0, (PTR32)(uintptr_t)bad, sizes[k][2] };
            bad[0] = sizes[k][0]; bad[1] = sizes[k][1];
            reset();
            assert( wow64_flush_calls( &wow64 ) == STATUS_INVALID_PARAMETER && !seen[0] );
        }
    }
    printf( "3000 binds in %d crossings", full );
    return 0;
}
'''

source = (fixture + 'typedef NTSTATUS (*unixlib_entry_t)( void *args );\n'
          'extern const unixlib_entry_t __wine_unix_call_wow64_funcs[funcs_count];\n' +
          header.replace('#undef UNIX_CALL\n#define UNIX_CALL( func, params ) (flush_calls(), WINE_UNIX_CALL( unix_ ## func, params ))\n',
                         '#undef UNIX_CALL\n#define UNIX_CALL( func, params ) (flush_calls(), WINE_UNIX_CALL( unix_ ## func, params ))\n') +
          pe + unix + test)
cc = os.environ.get('WINE_NX_HOST_CC') or shutil.which('clang') or 'cc'
with tempfile.TemporaryDirectory(prefix='wine-nx-gl-batch-') as directory:
    path = pathlib.Path(directory)
    (path / 'test.c').write_text(source)
    subprocess.run([cc, '-std=gnu11', '-g', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    run = subprocess.run([str(path / 'test')], capture_output=True, text=True, timeout=300)
    if run.returncode:
        raise SystemExit(run.stderr.strip()[-3000:])
print(f'opengl32 call batches: {len(deferred)} deferred thunks; {run.stdout.strip()}; the host runs them in order, with their arrays as made, '
      'across full batches, a missing array, a call made while a batch runs and batching off, and refuses broken batches')
