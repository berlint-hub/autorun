/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later.
 *
 * The SaltySD/FPSLocker bridge behind salty_display.h. The protocol is the one
 * NetherSX2_nx runs on the console: a raw CMIF request with the SFCI magic, a
 * second port ("NethFPS") if the SaltyNX build has it, the legacy "SaltySD"
 * port otherwise, and the FPSLocker block found by its magic in the shared
 * memory the service hands over.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "salty_display.h"
#include "display_sync.h"

#if defined(__SWITCH__)

#include <switch.h>

extern void wine_nx_runtime_trace(const char *msg) __attribute__((weak));

#define SALTYSD_PORT "NethFPS"
#define SALTYSD_LEGACY_PORT "SaltySD"
#define SFCI_MAGIC 0x49434653u
#define SFCO_MAGIC 0x4f434653u
#define IPC_REQUEST 4u
#define SALTYSD_GET_SHARED_MEMORY_HANDLE 7u
#define SALTYSD_GET_DISPLAY_REFRESH_RATE 10u
#define SALTYSD_SHARED_MEMORY_SIZE 0x1000u
/* NxFpsSharedBlock: the magic, then the fields this bridge touches. Several are
 * unaligned in the packed block, which is why they are copied through memcpy. */
#define NX_FPS_MAGIC 0x00465053u
#define NX_FPS_TARGET_OFFSET 10u
#define NX_FPS_SET_BUFFERS_OFFSET 56u
#define SESSION_COUNT_LIMIT 32u
#define POLL_INTERVAL_NS 250000000ull      /* the policy runs four times a second */
#define QUERY_INTERVAL_NS 1000000000ull    /* IPC: once a second at most */
#define STALE_NS 1000000000ull             /* a second without a frame: hold tight */

static volatile int s_enabled;
static volatile int s_thread_running;
static volatile int s_shutdown;
static volatile int s_shared_mapped;
static volatile int s_status;
static volatile uint32_t s_service_rate;
static volatile unsigned s_published_target;
static volatile unsigned s_measured_fps;
static volatile unsigned s_panel_rate;
static volatile uint64_t s_next_query_ns;
static volatile uint64_t s_next_poll_ns;
static volatile uint64_t s_last_frame_ns;
static uint8_t s_saved_target;
static int s_saved_valid;
static int s_fpslocker_owned;
static unsigned s_candidate_rate;
static uint64_t s_candidate_since_ns;
static uint8_t s_last_written;
static SharedMemory s_shared;
static Thread s_thread;
static uint8_t s_thread_stack[0x4000] __attribute__((aligned(0x1000)));

/* A frame's own work, in nanoseconds, as the present path reported it. */
static volatile uint64_t s_intervals[WINE_NX_DISPLAY_SYNC_SAMPLES];
static volatile uint32_t s_interval_count;
static volatile uint32_t s_interval_next;

static uint64_t now_ns(void)
{
    /* armGetSystemTick is cheaper and steadier than clock_gettime on Horizon;
     * it is monotonic and its frequency is the console's. */
    static uint64_t frequency;
    uint64_t tick;

    if (!frequency) frequency = armGetSystemTickFreq();
    if (!frequency) return 0;
    tick = armGetSystemTick();
    return tick / frequency * 1000000000ull + tick % frequency * 1000000000ull / frequency;
}

static void log_line(const char *text)
{
    if (wine_nx_runtime_trace) wine_nx_runtime_trace(text);
}

/* Raise the session limit once, before the first connect: a game's own startup
 * can use every session the process is allowed, and a connect then fails with
 * OutOfResource for a reason that has nothing to do with SaltySD. It is a
 * privileged call and may be refused -- the result is not worth an error. */
static void raise_session_limit_once(void)
{
    static int attempted;
    u64 limit = 0;
    Handle resource;

    if (attempted) return;
    attempted = 1;
    if (R_FAILED(svcGetInfo(&limit, InfoType_ResourceLimit, INVALID_HANDLE, 0)) || !limit) return;
    resource = (Handle)limit;
    svcSetResourceLimitLimitValue(resource, LimitableResource_Sessions, SESSION_COUNT_LIMIT);
    svcCloseHandle(resource);
}

/* One SFCI request over a session. out_words counts the reply's 64-bit words;
 * the reply's magic and its zero result are checked before anything is read. */
static int send_query(Handle session, uint32_t command, uint32_t request_bytes,
                      uint64_t *out, size_t out_words, Handle *out_handle)
{
    uint32_t *tls = (uint32_t *)armGetTls();
    uint64_t *request;
    uint32_t *cursor;
    unsigned copied = 0, moved = 0;
    Handle first = INVALID_HANDLE;
    uintptr_t address;
    uint32_t response_bytes;
    const uint64_t *raw;

    memset(tls, 0, 0x100);
    tls[0] = IPC_REQUEST;
    tls[1] = 0x80000000u | (request_bytes / 4u);
    tls[2] = 1;   /* a PID descriptor, as SaltySD's parser expects */
    request = (uint64_t *)((uint8_t *)tls + 0x20);
    request[0] = SFCI_MAGIC;
    request[1] = command;
    if (R_FAILED(svcSendSyncRequest(session))) return 0;

    {
        uint32_t control0 = tls[0], control1 = tls[1];
        cursor = tls + 2;
        if (control1 & 0x80000000u)
        {
            uint32_t special = *cursor++;
            if (special & 1u) cursor += 2;   /* the response's PID */
            copied = (special >> 1) & 15u;
            moved = (special >> 5) & 15u;
            if (copied + moved) first = (Handle)*cursor;
            cursor += copied + moved;
        }
        cursor += ((control0 >> 16) & 15u) * 2u;
        cursor += (((control0 >> 20) & 15u) + ((control0 >> 24) & 15u) + ((control0 >> 28) & 15u)) * 3u;
        response_bytes = (control1 & 0x1ffu) * 4u;
    }
    if (out_handle)
    {
        if (!copied || first == INVALID_HANDLE) return 0;
        *out_handle = first;
    }
    if (!out_words) return out_handle != NULL;
    address = ((uintptr_t)cursor + 15u) & ~(uintptr_t)15u;
    if (response_bytes < out_words * sizeof(uint64_t) ||
        address + out_words * sizeof(uint64_t) > (uintptr_t)tls + 0x100u)
        return 0;
    raw = (const uint64_t *)address;
    if ((uint32_t)raw[0] != SFCO_MAGIC || raw[1] != 0) return 0;
    if (out) memcpy(out, raw, out_words * sizeof(uint64_t));
    return 1;
}

/* Which port answered last time. A console running stock SaltyNX never has
 * NethFPS, and asking for it again every second costs a failed connect each
 * time; once the legacy name works it is tried first. */
static int s_prefer_legacy;
/* The log is the only place the console can say what happened, so the first
 * "no endpoint" and every change of rate are written once, not per second. */
static int s_logged_missing;

static Result connect_port(Handle *session)
{
    Result result;

    raise_session_limit_once();
    if (s_prefer_legacy)
    {
        result = svcConnectToNamedPort(session, SALTYSD_LEGACY_PORT);
        if (R_SUCCEEDED(result)) return result;
        s_prefer_legacy = 0;   /* the endpoint that answered is gone */
    }
    result = svcConnectToNamedPort(session, SALTYSD_PORT);
    if (R_SUCCEEDED(result)) return result;
    result = svcConnectToNamedPort(session, SALTYSD_LEGACY_PORT);
    if (R_SUCCEEDED(result)) s_prefer_legacy = 1;
    return result;
}

static int map_shared(Handle session)
{
    Handle handle = INVALID_HANDLE;

    if (s_shared_mapped) return 1;
    /* 32 bytes, not 16: SaltyNX's parser reads a raw body and a shorter one is
     * taken for an unsupported command. */
    if (!send_query(session, SALTYSD_GET_SHARED_MEMORY_HANDLE, 32, NULL, 2, &handle)) return 0;
    shmemLoadRemote(&s_shared, handle, SALTYSD_SHARED_MEMORY_SIZE, Perm_Rw);
    if (R_FAILED(shmemMap(&s_shared)))
    {
        shmemClose(&s_shared);
        memset(&s_shared, 0, sizeof(s_shared));
        return 0;
    }
    s_shared_mapped = 1;
    return 1;
}

static volatile uint8_t *find_target_field(void)
{
    uint8_t *base;
    size_t offset;

    if (!s_shared_mapped) return NULL;
    base = (uint8_t *)shmemGetAddr(&s_shared);
    if (!base) return NULL;
    for (offset = 0; offset + NX_FPS_SET_BUFFERS_OFFSET + 1 <= SALTYSD_SHARED_MEMORY_SIZE; offset += 4)
    {
        uint32_t magic;

        memcpy(&magic, base + offset, sizeof(magic));
        if (magic == NX_FPS_MAGIC) return (volatile uint8_t *)(base + offset);
    }
    return NULL;
}

/* Once a second: refresh rate and, if it is not mapped yet, the shared block.
 * Never under present_lock -- this runs on its own thread. */
static void query_service(void)
{
    uint64_t now = now_ns();
    Handle session = INVALID_HANDLE;
    Result result;
    uint64_t response[3] = {0, 0, 0};

    if (!now || now < s_next_query_ns) return;
    s_next_query_ns = now + QUERY_INTERVAL_NS;
    result = connect_port(&session);
    if (R_FAILED(result))
    {
        if (!s_logged_missing)
        {
            s_logged_missing = 1;
            log_line("[SYNC] no SaltySD endpoint (SaltyNX with the NethFPS port, or the "
                     "legacy name): the panel is left alone");
        }
        s_status = 5;
        s_service_rate = 0;
        return;
    }
    /* An endpoint that answers is an endpoint, even one too old to say what the
     * panel is doing: the status is about the decision, not the query. */
    if (s_status == 5) s_status = 0;
    /* The rate query is for the log and for the first guess; a build of SaltyNX
     * without it can still serve the shared block, and the policy starts from
     * the measured rate alone then. */
    if (send_query(session, SALTYSD_GET_DISPLAY_REFRESH_RATE, 32, response, 3, NULL) &&
        response[2] >= 20 && response[2] <= 240)
        /* The rate sits three words in, as NetherSX2 reads it. */
        s_service_rate = (uint32_t)response[2];
    if (!s_shared_mapped) map_shared(session);
    svcCloseHandle(session);
}

static void clear_samples(void)
{
    uint32_t i;

    for (i = 0; i < WINE_NX_DISPLAY_SYNC_SAMPLES; i++) s_intervals[i] = 0;
    s_interval_count = 0;
    s_interval_next = 0;
}

/* The rate the last window of frames says the game reaches. The window is read
 * without a lock -- the writer is the present thread -- so a slot can change
 * under the read; a torn window measures as nonsense, and a nonsense rate is
 * rejected by the estimator rather than published. */
static unsigned measure(unsigned *average_fps)
{
    uint64_t samples[WINE_NX_DISPLAY_SYNC_SAMPLES];
    uint32_t count = s_interval_count, i;
    struct wine_nx_display_sync_estimate estimate;

    if (average_fps) *average_fps = 0;
    if (count > WINE_NX_DISPLAY_SYNC_SAMPLES) count = WINE_NX_DISPLAY_SYNC_SAMPLES;
    /* The window is a ring: read it in the order it was written. */
    for (i = 0; i < count; i++)
        samples[i] = s_intervals[(s_interval_next + WINE_NX_DISPLAY_SYNC_SAMPLES - count + i) %
                                 WINE_NX_DISPLAY_SYNC_SAMPLES];
    estimate = wine_nx_display_sync_estimate(samples, count, 1000000000ull,
                                             WINE_NX_DISPLAY_SYNC_MIN_SAMPLES);
    if (average_fps) *average_fps = estimate.average_fps;
    s_measured_fps = estimate.upper_fps;
    return estimate.upper_fps;
}

/* 1 when the target may be written. FPSLocker itself writes a non-zero value
 * when the player chooses a rate, and that choice wins: this bridge only fills
 * a field nobody is using, and stops as soon as someone else takes it. */
static int target_is_ours(volatile uint8_t *target)
{
    uint8_t current = __atomic_load_n(target + NX_FPS_TARGET_OFFSET, __ATOMIC_ACQUIRE);

    if (!current || current == s_last_written)
    {
        s_fpslocker_owned = 0;
        return 1;
    }
    s_fpslocker_owned = 1;
    s_status = 3;
    return 0;
}

static void publish(uint64_t now)
{
    volatile uint8_t *block, *target;
    unsigned wanted, average = 0, measured;
    uint64_t stale_before = now > STALE_NS ? now - STALE_NS : 0;
    uint8_t current;

    /* No endpoint, nothing to go through: the rate query already said so, and a
     * "no block" would only hide it. */
    if (s_status == 5) return;
    block = find_target_field();
    if (!block)
    {
        s_status = 4;
        return;
    }
    target = block + NX_FPS_TARGET_OFFSET;
    if (!s_last_frame_ns || s_last_frame_ns < stale_before)
    {
        /* No frame for a second: a load screen or a paused game is not a
         * measurement, and the panel stays where it is. */
        clear_samples();
        return;
    }
    measured = measure(&average);
    if (!measured)
    {
        s_status = 1;
        return;
    }
    wanted = wine_nx_display_sync_next_rate(s_published_target ? s_published_target : s_service_rate,
                                             measured);
    if (!wanted)
    {
        s_status = 2;
        return;
    }
    if (!s_last_written && s_service_rate && wanted == s_service_rate)
    {
        /* The panel is already where the game is, and the field has not been
         * touched: leave it alone so whatever FPSLocker has stays. A game at
         * 60 fps or better never moves the panel. */
        s_panel_rate = s_service_rate;
        s_status = 2;
        return;
    }
    if (!target_is_ours(block)) return;
    current = __atomic_load_n(target, __ATOMIC_ACQUIRE);
    /* The field already says what it should, and it is ours: nothing to do. A
     * field someone cleared is not that -- the rate asked for is re-asserted,
     * after the same wait, so a player who clears FPSLocker's target gets the
     * panel back on the game. */
    if (wanted == s_last_written && current == s_last_written)
    {
        s_status = 2;
        return;
    }
    if (!wine_nx_display_sync_stable(wanted, &s_candidate_rate, &s_candidate_since_ns, now,
                                     (uint64_t)WINE_NX_DISPLAY_SYNC_STABLE_MS * 1000000ull))
        return;
    if (!s_saved_valid)
    {
        s_saved_target = current;
        s_saved_valid = 1;
    }

    {
        uint8_t expected = current;
        uint8_t value = (uint8_t)wanted;

        if (!__atomic_compare_exchange_n(target, &expected, value, false, __ATOMIC_ACQ_REL,
                                         __ATOMIC_ACQUIRE))
        {
            /* Someone wrote it between the read and the write: theirs. */
            s_fpslocker_owned = 1;
            s_status = 3;
            return;
        }
    }
    s_last_written = (uint8_t)wanted;
    s_published_target = wanted;
    s_panel_rate = wanted;
    s_candidate_since_ns = 0;
    s_status = 2;
    {
        char text[96];

        snprintf(text, sizeof(text), "[SYNC] panel at %u Hz, game at %u fps", wanted, measured);
        log_line(text);
    }
}

static void service_pass(void)
{
    uint64_t now = now_ns();

    /* Off means off: no query, no write, and nothing mapped for a game that has
     * not asked for any of it. */
    if (!__atomic_load_n(&s_enabled, __ATOMIC_ACQUIRE) || !now) return;
    query_service();
    if (now >= s_next_poll_ns)
    {
        s_next_poll_ns = now + POLL_INTERVAL_NS;
        publish(now);
    }
}

static void poller(void *argument)
{
    (void)argument;
    while (!__atomic_load_n(&s_shutdown, __ATOMIC_ACQUIRE))
    {
        if (!__atomic_load_n(&s_enabled, __ATOMIC_ACQUIRE))
        {
            svcSleepThread(POLL_INTERVAL_NS / 4);
            continue;
        }
        service_pass();
        svcSleepThread(POLL_INTERVAL_NS / 4);
    }
    s_thread_running = 0;
}

/* Everything one game session leaves behind: the value written, the value it
 * replaced, the measurement window, and what the last step decided. */
static void reset_session(void)
{
    s_last_written = 0;
    s_saved_valid = 0;
    s_fpslocker_owned = 0;
    s_published_target = 0;
    s_panel_rate = 0;
    s_measured_fps = 0;
    s_status = 0;           /* off is nothing yet, not what the last session said */
    s_prefer_legacy = 0;    /* the next game asks the fork's port again */
    s_logged_missing = 0;
    clear_samples();
}

void wine_nx_display_sync_configure(int enabled)
{
    if (!enabled)
    {
        __atomic_store_n(&s_enabled, 0, __ATOMIC_RELEASE);
        /* Hand FPSlocked back to whoever had it before. */
        if (s_shared_mapped && s_saved_valid && !s_fpslocker_owned && s_last_written)
        {
            volatile uint8_t *block = find_target_field();

            if (block)
            {
                uint8_t expected = s_last_written;

                __atomic_compare_exchange_n(block + NX_FPS_TARGET_OFFSET, &expected, s_saved_target,
                                            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
            }
        }
        reset_session();
        if (s_thread_running) log_line("[SYNC] panel rate following the game: off");
        return;
    }
    if (__atomic_load_n(&s_enabled, __ATOMIC_ACQUIRE)) return;
    __atomic_store_n(&s_shutdown, 0, __ATOMIC_RELEASE);
    clear_samples();
    s_next_query_ns = 0;
    s_next_poll_ns = 0;
    if (!s_thread_running)
    {
        /* Below the game's threads and on any core, like the log reader. */
        if (R_FAILED(threadCreate(&s_thread, poller, NULL, s_thread_stack, sizeof(s_thread_stack),
                                  0x3b, -2)))
        {
            log_line("[SYNC] could not create the display rate thread");
            return;
        }
        if (R_FAILED(threadStart(&s_thread)))
        {
            threadClose(&s_thread);
            log_line("[SYNC] could not start the display rate thread");
            return;
        }
        s_thread_running = 1;
    }
    __atomic_store_n(&s_enabled, 1, __ATOMIC_RELEASE);
    log_line("[SYNC] panel rate following the game: on");
}

void wine_nx_display_sync_note_frame(uint64_t busy_ns)
{
    uint64_t now;

    if (!s_enabled) return;
    now = now_ns();
    if (!now) return;
    s_last_frame_ns = now;
    /* Below 240 fps or above 10 fps is not a frame of a game that is drawing:
     * a load screen is neither, and a stale timestamp after a pause is a hole
     * rather than a 3-second frame. */
    if (busy_ns < 1000000000ull / 240u || busy_ns > 1000000000ull / 5u)
    {
        if (busy_ns > 1000000000ull / 5u) clear_samples();
        return;
    }
    {
        uint32_t next = s_interval_next;

        s_intervals[next] = busy_ns;
        s_interval_next = (next + 1) % WINE_NX_DISPLAY_SYNC_SAMPLES;
        if (s_interval_count < WINE_NX_DISPLAY_SYNC_SAMPLES) s_interval_count++;
    }
}

int wine_nx_display_sync_active(void)
{
    return __atomic_load_n(&s_enabled, __ATOMIC_ACQUIRE);
}

void wine_nx_display_sync_report(struct wine_nx_display_sync_report *report)
{
    if (!report) return;
    report->measured_fps = s_measured_fps;
    report->panel_rate = s_panel_rate;
    report->service_rate = s_service_rate;
    report->status = s_status;
}

/* Called once as the program ends: the thread goes, the block is closed and
 * FPSlocked is left as it was found. */
void wine_nx_display_sync_shutdown(void)
{
    int waits;

    __atomic_store_n(&s_shutdown, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&s_enabled, 0, __ATOMIC_RELEASE);
    /* A thread that will not stop must not hold the exit: it is given a fifth
     * of a second, and the block is closed under it if it does not come back. */
    for (waits = 0; s_thread_running && waits < 200; waits++) svcSleepThread(1000000);
    if (s_shared_mapped)
    {
        if (s_saved_valid && !s_fpslocker_owned && s_last_written)
        {
            volatile uint8_t *block = find_target_field();

            if (block)
            {
                uint8_t expected = s_last_written;

                __atomic_compare_exchange_n(block + NX_FPS_TARGET_OFFSET, &expected, s_saved_target,
                                            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
            }
        }
        shmemClose(&s_shared);
        memset(&s_shared, 0, sizeof(s_shared));
        s_shared_mapped = 0;
    }
    reset_session();
}

#ifdef WINE_NX_SALTY_TEST
/* The host test drives what the thread would: there is no scheduler here, the
 * shared block is a buffer, and time is moved by the test. */
void wine_nx_display_sync_test_pass(void)
{
    service_pass();
}
#endif

#else /* not the console: the runtime's host build */

void wine_nx_display_sync_configure(int enabled) { (void)enabled; }
void wine_nx_display_sync_note_frame(uint64_t busy_ns) { (void)busy_ns; }
int wine_nx_display_sync_active(void) { return 0; }
void wine_nx_display_sync_shutdown(void) {}
void wine_nx_display_sync_report(struct wine_nx_display_sync_report *report)
{
    if (report) memset(report, 0, sizeof(*report));
}

#endif
