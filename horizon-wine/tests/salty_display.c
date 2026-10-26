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
#define BLOCK_OFFSET 0x80u          /* the magic need not be at the start */
#define TARGET (BLOCK_OFFSET + 10u) /* FPSlocked within NxFpsSharedBlock */

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
static unsigned reply_rate;
static int rate_reply_missing;
static Handle shared_handle;

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
    uint64_t command;

    assert( session != INVALID_HANDLE );
    assert( request[0] == 0x49434653u );   /* SFCI_MAGIC */
    command = request[1];
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

Result threadCreate( Thread *thread, void (*entry)( void * ), void *argument, void *stack,
                     size_t size, int priority, int cpuid )
{
    (void)entry; (void)argument; (void)stack; (void)size; (void)priority; (void)cpuid;
    thread->created = 1;
    return 0;
}
Result threadStart( Thread *thread ) { return thread->created ? 0 : 1; }
void threadClose( Thread *thread ) { thread->created = 0; }

/* The runtime's log, replaced: what the bridge says, and how often. SaltySD's
 * own file is not written here; the count is what matters, since a line a second
 * is a log nobody can read. */
static int log_lines, log_missing, log_rates;
static char last_log[128];

void wine_nx_runtime_trace( const char *msg )
{
    log_lines++;
    snprintf( last_log, sizeof(last_log), "%s", msg );
    if (strstr( msg, "no SaltySD endpoint" )) log_missing++;
    if (strstr( msg, "panel at" )) log_rates++;
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
    log_lines = log_missing = log_rates = 0;
    last_log[0] = 0;
    reply_rate = 60;
    rate_reply_missing = 0;
    shared_handle = 0x200;
}

static void put_magic( void )
{
    uint32_t magic = 0x00465053u;   /* NX_FPS_MAGIC */

    memcpy( shared_memory + BLOCK_OFFSET, &magic, sizeof(magic) );
}

static unsigned target_value( void )
{
    return shared_memory[TARGET];
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
    assert( status() == 5 );
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
    assert( report.panel_rate == 50 && report.service_rate == 60 );
    assert( report.measured_fps >= 47 && report.measured_fps <= 49 );
    assert( report.status == 2 );
    /* 60 -> 55 -> 50: two changes, two lines, and the last says where it got to. */
    assert( log_rates == 2 );
    assert( strstr( last_log, "panel at 50 Hz" ) );

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
    assert( status() == 3 );

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
    assert( status() == 4 );
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
    assert( status() == 0 );

    wine_nx_display_sync_configure( 1 );
    assert( wine_nx_display_sync_active() );
    wine_nx_display_sync_configure( 0 );
    assert( !wine_nx_display_sync_active() );
    run_for( 48, 4000 );
    assert( target_value() == 0 );
}

int main( void )
{
    check_no_endpoint();
    check_off();
    check_legacy_port();
    check_fast_game();
    check_slow_game();
    check_very_slow_game();
    check_no_rate_query();
    check_unmapped_block();
    printf( "salty_display: all tests passed\n" );
    return 0;
}
