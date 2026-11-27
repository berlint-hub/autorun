#!/usr/bin/env python3
"""What a slow select waited for, as a slow frame's report names it.

horizon_server_describe_select (dlls/ntdll/unix/horizon.c) runs on the host
against a handle table of its own: the objects named by their kind, a handle
the server does not know, the kinds of wait and the timeout a call was made
with, worked back from the deadline left and the time it took.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "dlls/ntdll/unix/horizon.c").read_text()


def definition(start):
    begin = source.index(start)
    pos = source.index("{", begin) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[begin:pos] + "\n"


def enum(name):
    match = re.search(r"enum " + name + r"\s*\{[^}]+\};", source)
    assert match, name
    return match.group(0) + "\n"


fixture = r'''
#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef struct { long long QuadPart; } LARGE_INTEGER;
''' + enum("horizon_select_opcode") + enum("horizon_server_object_type") + r'''
struct horizon_select_wait_op { int op; unsigned int handles[1]; };
struct horizon_select_signal_and_wait_op { int op; unsigned int wait; unsigned int signal; };
struct horizon_server_object { int type; };
struct horizon_server_handle_entry { unsigned int handle; struct horizon_server_object *object; };
static pthread_mutex_t horizon_server_objects_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct horizon_server_object event = { HORIZON_SERVER_OBJECT_EVENT },
    queue = { HORIZON_SERVER_OBJECT_MSG_QUEUE }, timer = { HORIZON_SERVER_OBJECT_TIMER },
    odd = { 99 };
static struct horizon_server_handle_entry entries[] =
    { { 0x10, &event }, { 0x14, &queue }, { 0x18, &timer }, { 0x1c, &odd } };
static int lookups_locked;
static struct horizon_server_handle_entry *horizon_server_find_handle_locked( unsigned int handle )
{
    unsigned int i;

    assert( pthread_mutex_trylock( &horizon_server_objects_mutex ) );
    lookups_locked++;
    for (i = 0; i < sizeof(entries) / sizeof(entries[0]); i++)
        if (entries[i].handle == handle) return &entries[i];
    return NULL;
}
/* 100 ns units: the monotonic counter at 1 s, the wall clock at 100 s. */
static long long perf_now = 10000000, system_now = 1000000000;
static unsigned int NtQueryPerformanceCounter( LARGE_INTEGER *now, void *frequency )
{
    (void)frequency;
    now->QuadPart = perf_now;
    return 0;
}
static unsigned int NtQuerySystemTime( LARGE_INTEGER *now )
{
    now->QuadPart = system_now;
    return 0;
}
'''

tests = r'''
static char buf[224];

static void describe( const void *op, unsigned int size, long long timeout, long long elapsed )
{
    lookups_locked = 0;
    horizon_server_describe_select( op, size, timeout, elapsed, buf, sizeof(buf) );
    assert( pthread_mutex_trylock( &horizon_server_objects_mutex ) == 0 );
    pthread_mutex_unlock( &horizon_server_objects_mutex );
}

static void expect( const char *text )
{
    if (strcmp( buf, text )) { fprintf( stderr, "got \"%s\", wanted \"%s\"\n", buf, text ); assert( 0 ); }
}

int main( void )
{
    unsigned int wait[6] = { HORIZON_SELECT_WAIT, 0x10 };
    struct horizon_select_signal_and_wait_op both = { HORIZON_SELECT_SIGNAL_AND_WAIT, 0x18, 0x10 };
    int keyed[4] = { HORIZON_SELECT_KEYED_EVENT_WAIT };

    /* An event with no timeout: Doom 3's thread waiting for its tic. */
    describe( wait, 8, 0x7fffffffffffffffLL, 440000 );
    expect( "wait for 1: event; no timeout" );
    assert( lookups_locked == 1 );
    /* Several objects, one the server does not know, one of a kind without a
     * name, and more than are named; a timeout of 50 ms made 44 ms ago, of
     * the monotonic clock (negative), 6 ms of it left. */
    wait[1] = 0x14; wait[2] = 0x20; wait[3] = 0x1c; wait[4] = 0x18;
    describe( wait, 4 + 5 * 4, -(perf_now + 60000), 440000 );
    expect( "wait for 5: message queue, handle 0x20, object, ...; timeout 50 ms" );
    wait[0] = HORIZON_SELECT_WAIT_ALL;
    describe( wait, 4 + 2 * 4, system_now + 100000, 50000 );
    expect( "wait for all of 2: message queue, handle 0x20; timeout 15 ms" );
    describe( wait, 4, 0, 0 );
    expect( "a wait for nothing; a poll" );
    describe( &both, sizeof(both), 0x7fffffffffffffffLL, 0 );
    expect( "signal event and wait for timer; no timeout" );
    describe( keyed, sizeof(keyed), 0x7fffffffffffffffLL, 0 );
    expect( "a keyed event wait; no timeout" );
    assert( !lookups_locked );
    /* A sleep: no objects at all, and a timeout that passed. */
    describe( NULL, 0, -(perf_now - 10000), 160000 );
    expect( "no objects; timeout 15 ms" );
    /* A short buffer is cut, not overrun. */
    horizon_server_describe_select( wait, 4 + 2 * 4, 0, 0, buf, 12 );
    assert( strlen( buf ) == 11 );
    puts( "Slow server calls: what a select waited for, by object kind and timeout, passed" );
    return 0;
}
'''

code = fixture + definition("static const char *horizon_server_handle_kind_locked(") + \
       definition("void horizon_server_describe_select(") + tests
with tempfile.TemporaryDirectory(prefix="slow-server-calls-") as tmp:
    c, exe = Path(tmp) / "test.c", Path(tmp) / "test"
    c.write_text(code)
    subprocess.run(["clang", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-pthread",
                    "-fsanitize=address,undefined", str(c), "-o", str(exe)], check=True)
    print(subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=60).stdout.strip())

server = (root / "dlls/ntdll/unix/server.c").read_text()
# The select is read before the call: its reply takes the request's place.
call = server[server.index("unsigned int server_call_unlocked( void *req_ptr )"):]
call = call[:call.index("\n}\n")]
assert call.index("select_request.timeout") < call.index("horizon_sync_call( req, &declined )")
assert "HORIZON_SLOW_CALL_MS * 1000000ull" in call
