#!/usr/bin/env python3
"""opengl32's thunks against what they replaced: alloc_map_context_object, and
set_context_attribute's remembered context.

The thunks of glBindTexture, glBindBufferARB, glBindProgramARB and the other
calls that take one object name called alloc_context_objects and then
map_context_objects; they now call alloc_map_context_object, which enters the
context's table once when the name has its object. This builds the functions
as dlls/opengl32/wgl.c has them, over stand-ins for the context, the unix side
and Wine's debug macros, and drives the two ways through the same random
binds, generations and deletions of names, in two worlds that start the same,
for each kind of context alloc_context_objects tells apart. Every bind must end
the same: whether the thunk returns early, the name it passes on, the errors
and the objects it creates, and the tables afterwards.

set_context_attribute( -1 ), which nearly every thunk calls, returns at once
for a context whose state it no longer tracks, which it remembers in the TEB:
it must still look the context up for its first such call, after another
context became current, and for an attribute it tracks, which must still be
written."""
import os
import pathlib
import re
import shutil
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
wgl = (root / 'dlls/opengl32/wgl.c').read_text()
thunks = (root / 'dlls/opengl32/thunks.c').read_text()
generator = (root / 'dlls/opengl32/make_opengl').read_text()


def between(start, end):
    return wgl[wgl.index(start):wgl.index(end, wgl.index(start))]


# The generated thunks: every single-name check is the combined one, with the
# name passed on as it comes back, and nothing reads the name in between.
singles = re.findall(r'if \(!alloc_map_context_object\( (\w+), &(\w+), (TRUE|FALSE) \)\) return[^;]*;\n', thunks)
assert len(singles) > 200, len(singles)
assert not re.search(r'alloc_context_objects\( \w+, 1, &', thunks)
for match in re.finditer(r'if \(!alloc_map_context_object\( \w+, &(\w+), \w+ \)\) return[^;]*;\n', thunks):
    name = match.group(1)
    rest = thunks[match.end():]
    assign = rest.index(f'    args.{name} = {name};\n')
    assert not re.search(r'\b' + name + r'\b', rest[:assign]), name
assert 'alloc_map_context_object( $map_type, &$pname, $alloc )' in generator

# set_context_attribute's context is forgotten where the current one changes,
# which is in one place only.
make_current = between('BOOL WINAPI wglMakeContextCurrentARB(', 'BOOL WINAPI wglShareLists(')
assert re.findall(r'glCurrentRC = ', wgl) == ['glCurrentRC = '] and 'teb->glCurrentRC = handle;' in make_current
forget = re.search(r'\n(    if \(teb->glReserved1\[WINE_GL_RESERVED_UNTRACKED_RC\] != handle\)\n'
                   r'        teb->glReserved1\[WINE_GL_RESERVED_UNTRACKED_RC\] = NULL;\n)', make_current)
assert forget, 'wglMakeContextCurrentARB must forget set_context_attribute\'s context when another becomes current'
forget = forget.group(1)
assert make_current.index('teb->glCurrentRC = handle;') < make_current.index(forget)
for path in (root / 'dlls').rglob('*.c'):
    if path.parent.name != 'opengl32' and 'glCurrentRC' in path.read_text(errors='replace'):
        assert not re.search(r'glCurrentRC\s*=[^=]', path.read_text(errors='replace')), path

tables = (between('#define L1_COUNT', '#define MAKE_OBJECT_CALL') +
          between('static void init_object_table(', 'struct display_lists\n{'))
objects = between('static struct object_table *get_object_table(', 'static GLuint get_pname_object_type(')
attributes = between('static const struct context_attribute_desc *find_context_attribute(',
                     '/***********************************************************************\n *      wglCopyContext')
reserved = re.search(r'#define WINE_GL_RESERVED_UNTRACKED_RC +\d+\n', wgl).group(0)

fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int BOOL; typedef unsigned int UINT; typedef unsigned int GLuint; typedef unsigned int GLenum;
typedef int LONG; typedef unsigned int GLbitfield; typedef void *HGLRC; typedef int SRWLOCK;
#define TRUE 1
#define FALSE 0
#define TRACE(...) do {} while (0)
#define WARN(...) do {} while (0)
#define ERR(...) do {} while (0)
#define InitializeSRWLock(l) (*(l) = 0)
#define AcquireSRWLockExclusive(l) (assert(!*(l)), *(l) = -1)
#define ReleaseSRWLockExclusive(l) (*(l) = 0)
#define AcquireSRWLockShared(l) (assert(*(l) >= 0), ++*(l))
#define ReleaseSRWLockShared(l) (--*(l))
#define InterlockedExchange(p, v) __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST)
#define ReadNoFence(p) __atomic_load_n(p, __ATOMIC_RELAXED)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define GL_INVALID_OPERATION 0x0502
#define GL_ARB_compatibility 0
#define GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT 0x1
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x1
#define GL_FRAMEBUFFER 0x8d40
#define GL_READ_FRAMEBUFFER 0x8ca8
#define GL_DRAW_FRAMEBUFFER 0x8ca9
#define GL_DEPTH_FUNC 0x0b74
#define GL_SHADE_MODEL 0x0b54
#define GL_DEPTH_BUFFER_BIT 0x100
#define GL_LIGHTING_BIT 0x40
enum object_type { OBJ_TYPE_BUFFER, OBJ_TYPE_DISPLAY_LIST, OBJ_TYPE_FRAMEBUFFER, OBJ_TYPE_MEMORY, OBJ_TYPE_PATH,
                   OBJ_TYPE_PROGRAM, OBJ_TYPE_RENDERBUFFER, OBJ_TYPE_SAMPLER, OBJ_TYPE_SEMAPHORE, OBJ_TYPE_SHADER,
                   OBJ_TYPE_SHADER_ATI, OBJ_TYPE_SHADER_EXT, OBJ_TYPE_TEXTURE, OBJ_TYPE_COUNT };
static const char *debugstr_object_type( enum object_type type ) { (void)type; return ""; }
'''

objects_fixture = fixture + tables + r'''
struct opengl_client_context { int major_version, minor_version, profile_mask, context_flags; unsigned char *extensions; };
struct display_lists { LONG modified, shared, busy; struct object_table tables[OBJ_TYPE_COUNT]; };
struct context { struct opengl_client_context base; struct display_lists *lists; };
struct world { struct context context; struct display_lists lists; unsigned char extensions[1];
               GLuint next_host; int errors, created; };
static struct world worlds[2], *current;
static struct context *get_current_context(void) { return current ? &current->context : NULL; }
static void set_gl_error( GLenum error ) { assert( error == GL_INVALID_OPERATION ); current->errors++; }
static GLuint create_object( enum object_type type ) { (void)type; current->created++; return 0x40000000 + ++current->next_host; }
''' + objects + r'''
static unsigned long long seed = 0x9e3779b97f4a7c15ull;
static unsigned int rnd( unsigned int n ) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return (unsigned int)(seed % n); }

static void setup( int kind )
{
    for (int w = 0; w < 2; w++)
    {
        struct world *world = worlds + w;
        memset( world, 0, sizeof(*world) );
        for (UINT i = 0; i < OBJ_TYPE_COUNT; i++)
        {
            /* as display_lists_create sets them up */
            init_object_table( world->lists.tables + i, i );
            world->lists.tables[i].shared = &world->lists.shared;
            world->lists.tables[i].busy = &world->lists.busy;
        }
        world->context.lists = &world->lists;
        world->context.base.extensions = world->extensions;
        /* compatibility 2.1, core 3.2, forward-compatible 3.0, 3.1 without ARB_compatibility */
        static const int versions[4][3] = { { 2, 1, 0 }, { 3, 2, 1 }, { 3, 0, 0 }, { 3, 1, 0 } };
        world->context.base.major_version = versions[kind][0];
        world->context.base.minor_version = versions[kind][1];
        world->context.base.profile_mask = versions[kind][2];
        world->context.base.context_flags = kind == 2;
    }
}

int main( void )
{
    static GLuint names[OBJ_TYPE_COUNT][64];
    long binds = 0, early = 0, made = 0;

    for (int kind = 0; kind < 4; kind++)
    {
        int count[OBJ_TYPE_COUNT] = {0};

        setup( kind );
        for (int step = 0; step < 60000; step++)
        {
            enum object_type type = rnd( OBJ_TYPE_COUNT );
            UINT op = rnd( 10 );
            GLuint name;

            if (op == 0 && type != OBJ_TYPE_MEMORY && type != OBJ_TYPE_SHADER)
            {
                /* glGen*: a host name, the same in both worlds */
                GLuint host = 0x1000 + step, got[2];
                for (int w = 0; w < 2; w++)
                {
                    current = worlds + w;
                    got[w] = host;
                    put_context_objects( type, 1, &got[w] );
                }
                assert( got[0] == got[1] );
                if (got[0] && count[type] < 64) names[type][count[type]++] = got[0];
                continue;
            }
            if (op == 1 && count[type])
            {
                GLuint del[2];
                name = names[type][rnd( count[type] )];
                for (int w = 0; w < 2; w++) { current = worlds + w; del[w] = name; del_context_objects( type, 1, &del[w] ); }
                assert( del[0] == del[1] );
                continue;
            }

            switch (rnd( 4 ))
            {
            case 0: name = 0; break;
            case 1: name = count[type] ? names[type][rnd( count[type] )] : 1; break;
            case 2: name = 1 + rnd( 40 ); break;
            default: name = rnd( 1u << 18 ); break;   /* across several id blocks */
            }
            BOOL extension = rnd( 2 );

            /* as the thunk was */
            GLuint old_name = name, old_args = 0xdeadbeef;
            current = worlds;
            BOOL old_went_on = alloc_context_objects( type, 1, &old_name, extension );
            if (old_went_on) old_args = *map_context_objects( type, 1, &old_name );

            /* as it is */
            GLuint new_name = name, new_args = 0xdeadbeef;
            current = worlds + 1;
            BOOL new_went_on = alloc_map_context_object( type, &new_name, extension );
            if (new_went_on) new_args = new_name;

            if (old_went_on != new_went_on || old_args != new_args ||
                worlds[0].errors != worlds[1].errors || worlds[0].created != worlds[1].created)
            {
                fprintf( stderr, "context %d step %d type %d name %#x extension %d: went on %d/%d, passed %#x/%#x, "
                         "errors %d/%d, created %d/%d\n", kind, step, type, name, extension, old_went_on, new_went_on,
                         old_args, new_args, worlds[0].errors, worlds[1].errors, worlds[0].created, worlds[1].created );
                return 1;
            }
            binds++;
            early += !new_went_on;
            if (worlds[1].created > made) made = worlds[1].created;
        }
        for (UINT type = 0; type < OBJ_TYPE_COUNT; type++)
        {
            struct object_table *a = worlds[0].lists.tables + type, *b = worlds[1].lists.tables + type;
            assert( a->implicit == b->implicit && a->min_free == b->min_free && !a->lock && !b->lock );
            for (GLuint id = 0; id < (1u << 18); id++)
                assert( get_object( a, id, TRUE ) == get_object( b, id, TRUE ) );
        }
        assert( !worlds[0].lists.busy && !worlds[1].lists.busy );
        for (int w = 0; w < 2; w++)
            for (UINT type = 0; type < OBJ_TYPE_COUNT; type++)
            {
                free_object_ids( worlds[w].lists.tables + type, worlds[w].lists.tables[type].host_ids, NULL );
                free_object_ids( worlds[w].lists.tables + type, worlds[w].lists.tables[type].client_ids, NULL );
            }
    }
    printf( "%ld binds the same both ways in 4 kinds of context (%ld refused, objects created on the way)", binds, early );
    return 0;
}
'''

attributes_fixture = fixture + reserved + r'''
typedef struct { HGLRC glCurrentRC; void *glReserved1[29]; } TEB;
static TEB teb_of_thread;
static TEB *NtCurrentTeb(void) { return &teb_of_thread; }
struct context { GLbitfield used; GLenum depth_func; GLenum shade_model; };
struct context_attribute_desc { GLenum name; GLbitfield bit; UINT offset; UINT size; };
static const struct context_attribute_desc context_attributes[] =
{
    { GL_SHADE_MODEL, GL_LIGHTING_BIT, offsetof(struct context, shade_model), sizeof(GLenum) },
    { GL_DEPTH_FUNC, GL_DEPTH_BUFFER_BIT, offsetof(struct context, depth_func), sizeof(GLenum) },
};
static int compare_context_attributes( const void *v1, const void *v2 )
{
    const struct context_attribute_desc *a1 = v1, *a2 = v2;
    return (int)a1->name - (int)a2->name;
}
static struct context contexts[3];
static int lookups;
static struct context *get_current_context(void)
{
    HGLRC current = NtCurrentTeb()->glCurrentRC;
    lookups++;
    return current ? &contexts[(uintptr_t)current - 1] : NULL;
}
static void make_current( HGLRC handle )
{
    TEB *teb = NtCurrentTeb();
    teb->glCurrentRC = handle;
''' + forget + r'''}
''' + attributes + r'''
int main( void )
{
    static const GLenum const_less = 0x201, flat = 0x1d00;
    int i;

    set_context_attribute( -1, NULL, 0 );                    /* no context: nothing */
    make_current( (HGLRC)1 );
    set_context_attribute( GL_DEPTH_FUNC, &const_less, sizeof(const_less) );
    assert( contexts[0].used == GL_DEPTH_BUFFER_BIT && contexts[0].depth_func == const_less );
    set_context_attribute( -1, NULL, 0 );                    /* gives up tracking: looks it up */
    assert( contexts[0].used == (GLbitfield)-1 && lookups == 3 );
    for (i = 0; i < 1000; i++) set_context_attribute( -1, NULL, 0 );
    assert( lookups == 3 );                                  /* and never again */
    set_context_attribute( GL_SHADE_MODEL, &flat, sizeof(flat) );
    assert( contexts[0].shade_model == flat && lookups == 4 ); /* a tracked one is still written */

    make_current( (HGLRC)2 );                                /* another context: its own first call */
    set_context_attribute( -1, NULL, 0 );
    assert( contexts[1].used == (GLbitfield)-1 && lookups == 5 );
    set_context_attribute( -1, NULL, 0 );
    assert( lookups == 5 );

    make_current( (HGLRC)3 );                                /* a third, still tracked */
    set_context_attribute( GL_DEPTH_FUNC, &const_less, sizeof(const_less) );
    assert( contexts[2].used == GL_DEPTH_BUFFER_BIT && lookups == 6 );
    make_current( (HGLRC)1 );                                /* back: looked up once more */
    set_context_attribute( -1, NULL, 0 );
    set_context_attribute( -1, NULL, 0 );
    assert( lookups == 7 && contexts[2].used == GL_DEPTH_BUFFER_BIT );
    make_current( NULL );
    set_context_attribute( -1, NULL, 0 );
    assert( lookups == 8 );
    printf( "the first unsupported attribute of a context looks it up, the next do not, a switch or a tracked one does" );
    return 0;
}
'''

cc = os.environ.get('WINE_NX_HOST_CC') or shutil.which('clang') or 'cc'
out = []
with tempfile.TemporaryDirectory(prefix='wine-nx-gl-binds-') as directory:
    path = pathlib.Path(directory)
    for name, source in (('objects', objects_fixture), ('attributes', attributes_fixture)):
        (path / f'{name}.c').write_text(source)
        subprocess.run([cc, '-std=gnu11', '-g', '-O1', '-Wall', '-Werror', '-Wno-unused-function',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        str(path / f'{name}.c'), '-o', str(path / name)], check=True)
        run = subprocess.run([str(path / name)], timeout=600, capture_output=True, text=True)
        if run.returncode:
            raise SystemExit(f'{name}: {run.stderr.strip()[-2000:]}')
        out.append(run.stdout)
print('opengl32 thunks: ' + '; '.join(out))
