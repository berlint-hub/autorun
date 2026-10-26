/*
 * Just enough of libnx for the SaltySD bridge (source/salty_display.c) to be
 * built and driven on this machine. The test in tests/salty_display.c supplies
 * everything declared here: a named-port table that can be made to fail, a
 * shared-memory handle whose contents the test owns, a system tick the test
 * moves by hand, and a TLS buffer whose replies are laid out the way SaltyNX
 * lays them out. Nothing here talks to a console.
 */
#ifndef WINE_NX_TEST_SALTY_SWITCH_H
#define WINE_NX_TEST_SALTY_SWITCH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
typedef uint32_t Result;
typedef uint32_t Handle;

#define INVALID_HANDLE ((Handle)0)
/* libnx's own definitions: 0 is success and anything else is a failure, not a
 * test of the top bit. */
#define R_SUCCEEDED(res) ((res) == 0)
#define R_FAILED(res) ((res) != 0)

typedef enum { InfoType_ResourceLimit = 9 } InfoType;
/* libnx's two, and no more: the console reports one or the other. */
typedef enum
{
    AppletOperationMode_Handheld = 0,
    AppletOperationMode_Console = 1,
} AppletOperationMode;
typedef enum
{
    OmmOperationMode_Handheld = 0,
    OmmOperationMode_Console = 1,
} OmmOperationMode;
/* libnx's names and values, so the bridge is compiled against what it meets. */
typedef enum
{
    LimitableResource_Memory = 0,
    LimitableResource_Threads = 1,
    LimitableResource_Events = 2,
    LimitableResource_TransferMemories = 3,
    LimitableResource_Sessions = 4,
} LimitableResource;
typedef enum { Perm_None, Perm_R, Perm_W, Perm_Rw } Permission;

typedef struct
{
    Handle handle;
    u32 size;
    void *addr;
    Permission perm;
} SharedMemory;

typedef struct
{
    int created;
    int finished;   /* the test decides when the thread's loop has come back */
} Thread;

typedef struct
{
    Handle handle;
} Waiter;

Result svcGetInfo( u64 *out, u32 id0, Handle handle, u64 id1 );
Result svcSetResourceLimitLimitValue( Handle resource, LimitableResource limit, u64 value );
Result svcCloseHandle( Handle handle );
Result svcConnectToNamedPort( Handle *out, const char *name );
void *armGetTls( void );
Result svcSendSyncRequest( Handle session );
u64 armGetSystemTick( void );
u64 armGetSystemTickFreq( void );
void svcSleepThread( s64 ns );
Result shmemLoadRemote( SharedMemory *shared, Handle handle, size_t size, Permission perm );
Result shmemMap( SharedMemory *shared );
void shmemClose( SharedMemory *shared );
void *shmemGetAddr( SharedMemory *shared );
Result threadCreate( Thread *thread, void (*entry)( void * ), void *argument, void *stack,
                     size_t size, int priority, int cpuid );
AppletOperationMode appletGetOperationMode( void );
Result ommInitialize( void );
void ommExit( void );
Result ommGetOperationMode( OmmOperationMode *mode );
Result threadStart( Thread *thread );
void threadClose( Thread *thread );
Waiter waiterForThread( Thread *thread );
Result waitSingle( Waiter waiter, u64 timeout );

#endif
