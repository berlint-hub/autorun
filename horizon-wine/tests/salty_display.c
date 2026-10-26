/*
 * Host test for the SaltySD bridge (source/salty_display.c), built with
 * tests/salty_shim/switch.h standing in for libnx. What is checked is what the
 * console decides: which port is asked for, how the CMIF reply is read, where
 * the FPSLocker block is found, which rate it is asked for, whose target wins,
 * and that FPSlocked is left as it was found.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "switch.h"

#include "salty_display.h"

#define TLS_SIZE 0x100u
#define REQUEST_OFFSET 0x20u
#define SHARED_SIZE 0x1000u
#define BLOCK_OFFSET 0x80u              /* the magic need not be at the start */
#define TARGET (BLOCK_OFFSET + 10u)     /* FPSlocked, handheld */
#define TARGET_DOCKED (BLOCK_OFFSET + 164u) /* FPSlockedDocked */

/* ---- the fake console ---------------------------------------------------- */

static uint8_t tls[TLS_SIZE] __attribute__((aligned(16)));
static uint8_t shared_memory[SHARED_SIZE] __attribute__((aligned(16)));
static uint64_t now_ticks;
static Handle next_handle = 100;

static const char *ports[8];
static int port_count;
static int all_ports_fail;    /* no SaltyNX at all */
static int nethfps_fails;     /* a SaltyNX without the second port */

static int shared_mapped;
static int shared_size_wrong;
static AppletOperationMode operation_mode = AppletOperationMode_Handheld;
static int operation_mode_fails;      /* omm cannot be asked: the applet fallback */
static int omm_opened;
/* A console with FPSLocker's display sync on moves the panel to whatever is
 * written into the target; with it off the rate stays where it was. */
static int panel_follows = 1;
static unsigned reply_rate;
static int rate_reply_missing;
static Handle shared_handle;
/* What the bridge asked the panel to go back to, and how often: the command
 * FPSLocker's own reset sends, used when the bytes are handed back. */
static int rate_requests;
static unsigned rate_request_value;

static void *tls_data_pointer( uint8_t *cursor )
{
    /* CMIF: the reply's raw data follows the descriptors, 16-byte aligned. */
    uintptr_t address = ((uintptr_t)cursor + 15u) & ~(uintptr_t)15u;

    assert( address + 8u * 4u <= (uintptr_t)tls + TLS_SIZE );
    return (void *)address;
}

static void reply_rate_query( void )
{
    uint32_t *data;

    /* control1: 6 data words (24 bytes), and no handle descriptors at all.
     * The rate is the third 64-bit word, which is where NetherSX2 reads it. */
    *(uint32_t *)(tls + 4) = 0x80000000u | 6u;
    *(uint32_t *)(tls + 8) = 0;   /* special header: no PID, no handles */
    data = tls_data_pointer( tls + 12 );
    data[0] = 0x4f434653u;        /* SFCO_MAGIC */
    data[1] = 0;
    data[4] = reply_rate;         /* bytes 16..19 */
}

static void reply_shared_handle( void )
{
    uint32_t *data;

    /* control1: 4 data words (16 bytes); special header carries one copied
     * handle. */
    *(uint32_t *)(tls + 4) = 0x80000000u | 4u;
    *(uint32_t *)(tls + 8) = (1u << 1) | 1u;   /* one copied handle, and a PID */
    *(uint32_t *)(tls + 12) = 0x1234;          /* PID, low */
    *(uint32_t *)(tls + 16) = 0x5678;          /* PID, high */
    *(uint32_t *)(tls + 20) = shared_handle;
    data = tls_data_pointer( tls + 24 );
    data[0] = 0x4f434653u;
    data[1] = 0;
}

Result svcSendSyncRequest( Handle session )
{
    const uint64_t *request = (const uint64_t *)(tls + REQUEST_OFFSET);
    uint64_t command, value;

    assert( session != INVALID_HANDLE );
    assert( request[0] == 0x49434653u );   /* SFCI_MAGIC */
    command = request[1];
    value = request[2];
    /* The reply replaces the request in the TLS, header and all. */
    memset( tls, 0, sizeof(tls) );
    switch (command)
    {
    case 10:
        if (rate_reply_missing) return 1;
        reply_rate_query();
        return 0;
    case 7:
        reply_shared_handle();
        return 0;
    case 11:
        /* SetDisplayRefreshRate: applied on the spot, no display sync needed --
         * and refused by a console whose panel cannot be moved, which is the
         * one the "does not follow" check models. */
        rate_requests++;
        rate_request_value = (unsigned)value;
        if (!panel_follows) return 1;
        reply_rate = (unsigned)value;
        return 0;
    default:
        return 1;
    }
}

Result svcConnectToNamedPort( Handle *out, const char *name )
{
    if (port_count < (int)(sizeof(ports) / sizeof(ports[0]))) ports[port_count] = name;
    port_count++;
    if (all_ports_fail || (nethfps_fails && !strcmp( name, "NethFPS" )))
        return 0x00010801u;   /* OutOfResource, what the legacy port answers */
    *out = next_handle++;
    return 0;
}

void *armGetTls( void ) { return tls; }
u64 armGetSystemTick( void ) { return now_ticks; }
u64 armGetSystemTickFreq( void ) { return 1000000000ull; }   /* a tick is a ns */
void svcSleepThread( s64 ns ) { (void)ns; }
Result svcCloseHandle( Handle handle ) { (void)handle; return 0; }
AppletOperationMode appletGetOperationMode( void )
{
    return operation_mode;
}

Result ommInitialize( void )
{
    omm_opened++;
    return 0;
}

void ommExit( void )
{
    assert( omm_opened );
    omm_opened--;
}

Result ommGetOperationMode( OmmOperationMode *mode )
{
    if (operation_mode_fails) return 1;
    *mode = operation_mode == AppletOperationMode_Console ? OmmOperationMode_Console : OmmOperationMode_Handheld;
    return 0;
}

Result svcGetInfo( u64 *out, u32 id0, Handle handle, u64 id1 )
{
    (void)handle; (void)id1;
    assert( id0 == 9 );    /* InfoType_ResourceLimit */
    *out = 0x40;           /* the process's resource limit handle */
    return 0;
}
Result svcSetResourceLimitLimitValue( Handle resource, LimitableResource limit, u64 value )
{
    (void)resource; (void)limit; (void)value;
    return 0;
}

Result shmemLoadRemote( SharedMemory *shared, Handle handle, size_t size, Permission perm )
{
    (void)size;
    assert( handle == shared_handle );
    shared->handle = handle;
    shared->perm = perm;
    return 0;
}

Result shmemMap( SharedMemory *shared )
{
    if (shared_size_wrong) return 1;
    shared->addr = shared_memory;
    shared->size = SHARED_SIZE;
    shared_mapped = 1;
    return 0;
}

void shmemClose( SharedMemory *shared ) { shared->addr = NULL; shared_mapped = 0; }
void *shmemGetAddr( SharedMemory *shared ) { return shared->addr; }

/* A Handle is 32 bits on the console; a pointer is not, so the fake keeps a
 * table and hands out an index, the way the kernel would. */
static Thread *created_threads[8];
static int thread_creates;

static int threads_live( void )
{
    int i, live = 0;

    for (i = 0; i < (int)(sizeof(created_threads) / sizeof(created_threads[0])); i++)
        if (created_threads[i]) live++;
    return live;
}

static int thread_slot( Thread *thread )
{
    int i;

    for (i = 0; i < (int)(sizeof(created_threads) / sizeof(created_threads[0])); i++)
        if (created_threads[i] == thread) return i;
    return -1;
}

Result threadCreate( Thread *thread, void (*entry)( void * ), void *argument, void *stack,
                     size_t size, int priority, int cpuid )
{
    int slot = -1, i;

    (void)entry; (void)argument; (void)stack; (void)size; (void)priority; (void)cpuid;
    for (i = 0; i < (int)(sizeof(created_threads) / sizeof(created_threads[0])); i++)
        if (!created_threads[i]) { slot = i; break; }
    assert( slot >= 0 );
    thread_creates++;
    created_threads[slot] = thread;
    thread->created = 1;
    thread->finished = 0;
    return 0;
}
Result threadStart( Thread *thread ) { return thread->created ? 0 : 1; }
void threadClose( Thread *thread )
{
    int slot = thread_slot( thread );

    /* libnx unmaps the stack and closes the handle: doing that to a thread that
     * is still running is the bug this test is here to catch. */
    assert( thread->finished );
    assert( slot >= 0 );
    created_threads[slot] = NULL;
    thread->created = 0;
}
Waiter waiterForThread( Thread *thread )
{
    int slot = thread_slot( thread );

    assert( slot >= 0 );
    return (Waiter){ (Handle)(slot + 1) };
}
Result waitSingle( Waiter waiter, u64 timeout )
{
    (void)timeout;
    assert( waiter.handle >= 1 && waiter.handle <= 8 );
    assert( created_threads[waiter.handle - 1] );
    /* The loop has seen the quit by the time the wait is asked, as it has. */
    created_threads[waiter.handle - 1]->finished = 1;
    return 0;
}

/* The runtime's log, replaced: what the bridge says, and how often. SaltySD's
 * own file is not written here; the count is what matters, since a line a second
 * is a log nobody can read. */
static int log_lines, log_missing, log_rates, log_unfollowed, log_mode;
static char last_log[128];

void wine_nx_runtime_trace( const char *msg )
{
    log_lines++;
    snprintf( last_log, sizeof(last_log), "%s", msg );
    if (strstr( msg, "no SaltySD endpoint" )) log_missing++;
    if (strstr( msg, "asked for" )) log_rates++;
    if (strstr( msg, "the panel is still at" )) log_unfollowed++;
    if (strstr( msg, "omm does not answer" )) log_mode++;
}

/* ---- the test's own helpers ---------------------------------------------- */

static void reset_console( void )
{
    memset( tls, 0, sizeof(tls) );
    memset( shared_memory, 0, sizeof(shared_memory) );
    now_ticks = 1000000000ull;   /* a second in, well away from zero */
    port_count = 0;
    all_ports_fail = 0;
    nethfps_fails = 0;
    shared_mapped = 0;
    shared_size_wrong = 0;
    operation_mode = AppletOperationMode_Handheld;
    operation_mode_fails = 0;
    panel_follows = 1;
    assert( omm_opened == 0 );
    /* Every check closes what it opened: the thread is created once per
     * program, and shutdown is what closes it. */
    assert( !threads_live() );
    thread_creates = 0;
    log_lines = log_missing = log_rates = log_unfollowed = log_mode = 0;
    last_log[0] = 0;
    reply_rate = 60;
    rate_reply_missing = 0;
    shared_handle = 0x200;
    rate_requests = 0;
    rate_request_value = 0;
}

static void put_magic_at( size_t offset )
{
    uint32_t magic = 0x00465053u;   /* NX_FPS_MAGIC */

    assert( offset + sizeof(magic) <= sizeof(shared_memory) );
    memcpy( shared_memory + offset, &magic, sizeof(magic) );
}

static void put_magic( void )
{
    put_magic_at( BLOCK_OFFSET );
}

static unsigned target_value( void )
{
    return shared_memory[TARGET];
}

static unsigned target_value_docked( void )
{
    return shared_memory[TARGET_DOCKED];
}

static void set_target( unsigned value )
{
    shared_memory[TARGET] = (uint8_t)value;
}

static void advance( uint64_t ms )
{
    now_ticks += ms * 1000000ull;
}

/* Frames of a game running at fps for ms milliseconds, as the present path
 * would report them, with the bridge's thread taking its steps in between. */
static void run_for( unsigned fps, uint64_t ms )
{
    uint64_t interval_ns = 1000000000ull / fps;
    uint64_t elapsed = 0;

    while (elapsed < ms)
    {
        unsigned i;

        /* A quarter of a second of frames, then the pass the thread would take
         * in that time. */
        for (i = 0; i < fps / 4u; i++)
        {
            wine_nx_display_sync_note_frame( interval_ns );
            now_ticks += interval_ns;
            elapsed += interval_ns / 1000000ull;
        }
        wine_nx_display_sync_test_pass();
        /* SaltyNX applies the wanted rate itself, about once a second: the fake
         * console does the same, unless a test says its panel is stuck. */
        if (panel_follows && operation_mode == AppletOperationMode_Handheld && shared_memory[TARGET])
            reply_rate = shared_memory[TARGET];
        if (panel_follows && operation_mode == AppletOperationMode_Console && shared_memory[TARGET_DOCKED])
            reply_rate = shared_memory[TARGET_DOCKED];
    }
}

/* The pass cadence drifts: a status assertion is only meaningful after a pass
 * that certainly ran. */
static void settle( void )
{
    advance( 400 );
    wine_nx_display_sync_test_pass();
    wine_nx_display_sync_test_pass();
}

static int status( void )
{
    struct wine_nx_display_sync_report report;

    memset( &report, 0, sizeof(report) );
    wine_nx_display_sync_report( &report );
    return report.status;
}

/* ---- the checks ---------------------------------------------------------- */

/* Asking twice is one thread, and turning the feature off and on again reuses
 * it: the stack is mapped once for the life of the program, and shutdown is the
 * one place it is taken away. */
static void check_double_start( void )
{
    struct wine_nx_display_sync_report report;

    reset_console();
    put_magic();
    wine_nx_display_sync_configure( 1 );
    wine_nx_display_sync_configure( 1 );
    assert( thread_creates == 1 );
    run_for( 48, 12000 );
    assert( target_value() == 50 );

    /* Off puts the byte back at once; on again does not map a second thread. */
    wine_nx_display_sync_configure( 0 );
    assert( target_value() == 0 );
    wine_nx_display_sync_report( &report );
    assert( report.status == WINE_NX_DISPLAY_SYNC_NOTHING );
    wine_nx_display_sync_configure( 1 );
    assert( thread_creates == 1 );
    run_for( 48, 12000 );
    assert( target_value() == 50 );

    wine_nx_display_sync_shutdown();
    assert( target_value() == 0 );
    wine_nx_display_sync_configure( 0 );
}

/* Shutdown on a bridge that was never turned on: nothing has been mapped, no
 * thread exists, and nothing may be written. */
static void check_shutdown_without_start( void )
{
    reset_console();
    put_magic();
    wine_nx_display_sync_shutdown();
    assert( !shared_mapped && !threads_live() );
    assert( target_value() == 0 && target_value_docked() == 0 );
    wine_nx_display_sync_configure( 0 );
}

/* The shared memory is there but the FPSLocker block is not yet: the bridge
 * says so and waits for the magic instead of writing at a guessed address. */
static void check_late_block( void )
{
    reset_console();
    wine_nx_display_sync_configure( 1 );
    run_for( 48, 6000 );
    assert( shared_mapped );
    assert( target_value() == 0 );
    settle();
    assert( status() == WINE_NX_DISPLAY_SYNC_NO_BLOCK );

    put_magic();   /* the plugin loads a moment later */
    run_for( 48, 12000 );
    assert( target_value() == 50 );
    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );
}

/* A magic too close to the end of the shared memory is not a block: the whole
 * 310-byte structure has to fit, or the fields this bridge writes would land
 * outside the mapping. Nothing is written, and the scan says so. */
static void check_short_block_is_not_a_block( void )
{
    reset_console();
    put_magic_at( sizeof(shared_memory) - 100u );   /* 210 bytes short */
    wine_nx_display_sync_configure( 1 );
    run_for( 48, 6000 );
    settle();
    assert( status() == WINE_NX_DISPLAY_SYNC_NO_BLOCK );
    /* Nothing was written anywhere in the block, and nothing past the end. */
    {
        size_t i;
        unsigned written = 0;

        for (i = 0; i < sizeof(shared_memory); i++) if (shared_memory[i]) written++;
        /* The magic itself ("FPS" and a NUL byte) and nothing else. */
        assert( written == 3 );
    }
    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );
}

/* SaltyNX starts after the game: no endpoint at first, and it joins in when it
 * appears without a restart. */
static void check_late_endpoint( void )
{
    reset_console();
    put_magic();
    all_ports_fail = 1;
    wine_nx_display_sync_configure( 1 );
    run_for( 48, 4000 );
    settle();
    assert( status() == WINE_NX_DISPLAY_SYNC_NO_ENDPOINT );
    assert( target_value() == 0 );

    all_ports_fail = 0;
    run_for( 48, 12000 );
    assert( shared_mapped );
    assert( target_value() == 50 );
    wine_nx_display_sync_shutdown();
    assert( target_value() == 0 );
    wine_nx_display_sync_configure( 0 );
}

/* Handing the bytes back also puts the panel back on 60, with the command
 * FPSLocker's own reset sends: SaltyNX keeps the last rate it was given, so
 * clearing the byte alone would leave a slow game's rate on the panel. */
static void check_panel_reset( void )
{
    reset_console();
    put_magic();
    wine_nx_display_sync_configure( 1 );
    run_for( 48, 12000 );
    assert( target_value() == 50 );
    assert( rate_requests == 0 );           /* nothing is asked while it follows */
    wine_nx_display_sync_configure( 0 );
    assert( target_value() == 0 );
    assert( rate_requests == 1 && rate_request_value == 60 );
    assert( reply_rate == 60 );             /* and the panel is really back on 60 */
    /* Nothing was written any more, so closing the session asks for nothing. */
    wine_nx_display_sync_shutdown();
    assert( rate_requests == 1 );

    /* A game that never needed a rate leaves the panel alone entirely. */
    reset_console();
    put_magic();
    wine_nx_display_sync_configure( 1 );
    run_for( 60, 12000 );
    assert( target_value() == 0 && rate_requests == 0 );
    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );
    assert( rate_requests == 0 );

    /* Docked, the rate that was applied is put back to 60 as well -- the number
     * SaltyNX's own cleanup uses when the app ends. */
    reset_console();
    put_magic();
    operation_mode = AppletOperationMode_Console;
    reply_rate = 120;
    wine_nx_display_sync_configure( 1 );
    run_for( 48, 12000 );
    assert( target_value_docked() == 50 );
    wine_nx_display_sync_shutdown();
    assert( target_value_docked() == 0 );
    assert( rate_requests == 1 && rate_request_value == 60 );
    wine_nx_display_sync_configure( 0 );
}

/* No SaltySD on the console at all: nothing is written, nothing crashes, and
 * the report says so. */
static void check_no_endpoint( void )
{
    struct wine_nx_display_sync_report report;

    reset_console();
    put_magic();
    all_ports_fail = 1;   /* no SaltyNX running at all */
    wine_nx_display_sync_configure( 1 );
    assert( port_count == 0 );            /* nothing is touched until the first pass */
    advance( 1 );
    wine_nx_display_sync_test_pass();
    /* NethFPS first, then the legacy name: the order NetherSX2 settled on. */
    assert( port_count == 2 );
    assert( !strcmp( ports[0], "NethFPS" ) && !strcmp( ports[1], "SaltySD" ) );
    run_for( 48, 4000 );
    assert( port_count > 3 );             /* it keeps asking, and gives up each time */
    assert( log_missing == 1 );           /* but says so once, not once a second */
    assert( target_value() == 0 );
    settle();
    assert( status() == WINE_NX_DISPLAY_SYNC_NO_ENDPOINT );
    wine_nx_display_sync_report( &report );
    assert( report.service_rate == 0 && report.panel_rate == 0 );

    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );
    assert( target_value() == 0 );
}

/* A console running stock SaltyNX: there is no NethFPS, the legacy name works
 * once, and it is the one asked for from then on instead of a failed connect a
 * second to a port that does not exist. */
static void check_legacy_port( void )
{
    reset_console();
    put_magic();
    nethfps_fails = 1;
    wine_nx_display_sync_configure( 1 );
    advance( 1 );
    wine_nx_display_sync_test_pass();
    assert( port_count == 2 );
    assert( !strcmp( ports[0], "NethFPS" ) && !strcmp( ports[1], "SaltySD" ) );
    assert( shared_mapped );
    advance( 3000 );
    wine_nx_display_sync_test_pass();
    assert( port_count == 3 && !strcmp( ports[2], "SaltySD" ) );   /* one try now */
    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );
}

/* A game at 48 fps: the panel is asked down to 50 Hz, one step at a time, and
 * FPSlocked is zero again once the game has gone. */
static void check_slow_game( void )
{
    struct wine_nx_display_sync_report report;
    unsigned first;

    reset_console();
    put_magic();
    wine_nx_display_sync_configure( 1 );
    advance( 1 );
    wine_nx_display_sync_test_pass();
    assert( shared_mapped );
    assert( port_count == 1 && !strcmp( ports[0], "NethFPS" ) );

    /* Nothing yet: 48 fps needs 60 frames before a rate is published, and the
     * panel is not moved on the strength of a load screen. */
    run_for( 48, 500 );
    assert( target_value() == 0 );

    run_for( 48, 12000 );
    first = target_value();
    /* Not 45: a 48 fps game on a 45 Hz panel drops a frame every nine. */
    assert( first == 50 );
    wine_nx_display_sync_report( &report );
    /* service_rate is what the panel is really at, and the fake console has
     * followed the write, as a real one with display sync on does. */
    assert( report.panel_rate == 50 && report.service_rate == 50 );
    assert( report.measured_fps >= 47 && report.measured_fps <= 49 );
    assert( report.status == WINE_NX_DISPLAY_SYNC_FOLLOWING );
    /* 60 -> 55 -> 50, one step per two seconds, and the last line says both
     * where the rate goes and what it is following. */
    assert( log_rates == 2 );
    assert( strstr( last_log, "asked for 50 Hz (handheld), game at 48 fps" ) );

    /* A pause is not a measurement: three seconds without a frame leave the
     * panel where the last real one put it. */
    advance( 3000 );
    wine_nx_display_sync_test_pass();
    assert( target_value() == 50 );

    /* The game's own FPSLocker choice now wins: it is not written over. */
    set_target( 45 );
    run_for( 48, 6000 );
    assert( target_value() == 45 );
    settle();
    assert( status() == WINE_NX_DISPLAY_SYNC_FPSLOCKER );

    /* Clearing it hands the field back to the bridge. */
    set_target( 0 );
    run_for( 48, 6000 );
    assert( target_value() == 50 );

    /* The game ends: FPSlocked goes back to what it was before, which here is
     * the empty field FPSLocker leaves when it has no opinion. */
    wine_nx_display_sync_shutdown();
    assert( target_value() == 0 );
    assert( !shared_mapped );
    wine_nx_display_sync_configure( 0 );
}

/* A game well under 45 fps still asks for 45 rather than leaving the panel at
 * 60 dropping frames: it goes down a step at a time. */
static void check_very_slow_game( void )
{
    reset_console();
    put_magic();
    wine_nx_display_sync_configure( 1 );
    run_for( 30, 1000 );
    assert( target_value() == 0 );     /* nothing before a rate is known */
    run_for( 30, 20000 );
    assert( target_value() == 45 );
    wine_nx_display_sync_shutdown();
    assert( target_value() == 0 );
    wine_nx_display_sync_configure( 0 );
}

/* Docked, the panel runs faster than the table this policy knows, and SaltyNX
 * reads the docked byte, not the handheld one. A game at 60 there is not asking
 * for less, and asking for 60 would be a downgrade; a game that cannot hold 60
 * is slowed down through FPSlockedDocked and leaves FPSlocked alone. */
static void check_docked( void )
{
    reset_console();
    put_magic();
    operation_mode = AppletOperationMode_Console;
    reply_rate = 120;
    wine_nx_display_sync_configure( 1 );
    run_for( 60, 12000 );
    assert( target_value() == 0 && target_value_docked() == 0 );
    assert( status() == WINE_NX_DISPLAY_SYNC_FOLLOWING );
    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );

    reset_console();
    put_magic();
    operation_mode = AppletOperationMode_Console;
    reply_rate = 120;
    wine_nx_display_sync_configure( 1 );
    run_for( 48, 30000 );
    /* 120 -> 60 -> 55 -> 50: one step per two seconds, and no further. The
     * handheld byte stays at zero the whole time. */
    assert( target_value_docked() == 50 );
    assert( target_value() == 0 );
    wine_nx_display_sync_shutdown();
    assert( target_value_docked() == 0 );
    wine_nx_display_sync_configure( 0 );
}

/* Docking in the middle of a game: the byte of the mode being left goes back,
 * and the rate carries on in the byte SaltyNX is now reading. */
static void check_dock_undock( void )
{
    reset_console();
    put_magic();
    wine_nx_display_sync_configure( 1 );
    run_for( 48, 12000 );
    assert( target_value() == 50 && target_value_docked() == 0 );

    operation_mode = AppletOperationMode_Console;
    reply_rate = 120;
    run_for( 48, 6000 );
    assert( target_value() == 0 );            /* back to what it was handheld */
    assert( target_value_docked() == 50 );

    operation_mode = AppletOperationMode_Handheld;
    reply_rate = 60;
    run_for( 48, 6000 );
    assert( target_value() == 50 );
    assert( target_value_docked() == 0 );

    wine_nx_display_sync_shutdown();
    assert( target_value() == 0 && target_value_docked() == 0 );
    wine_nx_display_sync_configure( 0 );
}

/* A game that comes back up to 60 fps gets its panel back: the rate the bridge
 * asked for is handed back instead of staying on the byte for the session. */
static void check_recovery( void )
{
    reset_console();
    put_magic();
    wine_nx_display_sync_configure( 1 );
    run_for( 48, 12000 );
    assert( target_value() == 50 );

    /* 60 goes back into the byte, which is what SaltyNX applies: the old value
     * would not raise the panel, since SaltyNX keeps the last rate it was
     * given. FPSLocker's own reset writes the same number. */
    run_for( 60, 6000 );
    assert( target_value() == 60 );
    settle();
    assert( status() == WINE_NX_DISPLAY_SYNC_FOLLOWING );

    /* And it comes back down again: the hand-back is not a one-way door. */
    run_for( 48, 12000 );
    assert( target_value() == 50 );
    wine_nx_display_sync_shutdown();
    assert( target_value() == 0 );
    wine_nx_display_sync_configure( 0 );
}

/* Without omm the mode comes from libnx's cached applet value, and the panel
 * rate is the tie-breaker: handheld tops out at 60 Hz. */
static void check_mode_fallback( void )
{
    reset_console();
    put_magic();
    operation_mode_fails = 1;
    operation_mode = AppletOperationMode_Console;
    reply_rate = 120;
    wine_nx_display_sync_configure( 1 );
    run_for( 48, 12000 );
    assert( target_value_docked() == 50 );
    assert( target_value() == 0 );
    /* Said once, not once a pass: the fallback is worth knowing about, and a
     * line four times a second is not. */
    assert( log_mode == 1 );
    run_for( 48, 6000 );
    assert( log_mode == 1 );
    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );

    /* With omm answering, nothing is said about it. */
    reset_console();
    put_magic();
    wine_nx_display_sync_configure( 1 );
    run_for( 48, 6000 );
    assert( log_mode == 0 );
    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );

    /* The cached value says handheld, but a panel above 60 Hz is docked. */
    reset_console();
    put_magic();
    operation_mode_fails = 1;
    operation_mode = AppletOperationMode_Handheld;
    reply_rate = 120;
    wine_nx_display_sync_configure( 1 );
    run_for( 48, 12000 );
    assert( target_value_docked() == 50 );
    assert( target_value() == 0 );
    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );
}

/* Asking for a rate the panel never moves to is the one failure the log has to
 * name, because it is almost always FPSLocker's display sync being off. */
static void check_panel_does_not_follow( void )
{
    reset_console();
    put_magic();
    reply_rate = 120;   /* docked, and it stays there however much is asked */
    panel_follows = 0;
    operation_mode = AppletOperationMode_Console;
    wine_nx_display_sync_configure( 1 );
    run_for( 48, 12000 );
    assert( target_value_docked() == 50 );
    /* Three seconds after the first request, and once only: a line a second is
     * a log nobody reads. */
    assert( log_unfollowed == 1 );
    run_for( 48, 8000 );
    assert( log_unfollowed == 1 );

    /* A panel that does move says nothing. */
    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );
    reset_console();
    put_magic();
    wine_nx_display_sync_configure( 1 );
    run_for( 48, 12000 );
    assert( target_value() != 0 );
    assert( log_unfollowed == 0 );
    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );
}

/* Frames the present path should not count: a slide show and a hitch that has
 * no business being read as a rate. */
static void check_note_frame_filters( void )
{
    reset_console();
    put_magic();
    wine_nx_display_sync_configure( 1 );
    advance( 1 );
    wine_nx_display_sync_test_pass();
    /* A three-second "frame" empties the window instead of joining it. */
    wine_nx_display_sync_note_frame( 3000000000ull );
    /* And a 400 fps one is not a frame of anything that is drawing. */
    wine_nx_display_sync_note_frame( 2500000ull );
    run_for( 48, 12000 );
    assert( target_value() == 50 );   /* the real frames still decide */
    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );
}

/* A game that holds 60 or better never puts the panel down. */
static void check_fast_game( void )
{
    reset_console();
    put_magic();
    reply_rate = 60;
    wine_nx_display_sync_configure( 1 );
    run_for( 60, 12000 );
    assert( target_value() == 0 );
    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );
}

/* A build of SaltyNX without the rate query can still serve the shared block:
 * the target starts from nothing and the first step is taken from the measured
 * rate alone. */
static void check_no_rate_query( void )
{
    reset_console();
    put_magic();
    rate_reply_missing = 1;
    wine_nx_display_sync_configure( 1 );
    advance( 1 );
    wine_nx_display_sync_test_pass();
    assert( shared_mapped );
    run_for( 48, 12000 );
    assert( target_value() == 50 );
    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );
}

/* A block that cannot be mapped is not a reason to write anywhere; the bridge
 * keeps polling instead of guessing an address. */
static void check_unmapped_block( void )
{
    reset_console();
    put_magic();
    shared_size_wrong = 1;
    wine_nx_display_sync_configure( 1 );
    assert( !shared_mapped );
    run_for( 48, 6000 );
    assert( target_value() == 0 );
    settle();
    assert( status() == WINE_NX_DISPLAY_SYNC_NO_BLOCK );
    wine_nx_display_sync_shutdown();
    wine_nx_display_sync_configure( 0 );
}

/* The off switch: nothing is started, nothing is written, and asking twice is
 * still nothing. */
static void check_off( void )
{
    reset_console();
    put_magic();
    wine_nx_display_sync_configure( 0 );
    assert( !wine_nx_display_sync_active() );
    run_for( 48, 4000 );
    settle();
    assert( port_count == 0 && target_value() == 0 );
    assert( status() == WINE_NX_DISPLAY_SYNC_NOTHING );

    wine_nx_display_sync_configure( 1 );
    assert( wine_nx_display_sync_active() );
    wine_nx_display_sync_configure( 0 );
    assert( !wine_nx_display_sync_active() );
    run_for( 48, 4000 );
    assert( target_value() == 0 );
    wine_nx_display_sync_shutdown();
}

int main( void )
{
    check_no_endpoint();
    check_off();
    check_panel_reset();
    check_double_start();
    check_shutdown_without_start();
    check_late_block();
    check_short_block_is_not_a_block();
    check_late_endpoint();
    check_legacy_port();
    check_fast_game();
    check_docked();
    check_mode_fallback();
    check_dock_undock();
    check_panel_does_not_follow();
    check_note_frame_filters();
    check_slow_game();
    check_recovery();
    check_very_slow_game();
    check_no_rate_query();
    check_unmapped_block();
    printf( "salty_display: all tests passed\n" );
    return 0;
}
