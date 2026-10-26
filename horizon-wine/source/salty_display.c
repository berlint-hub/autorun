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
/* NxFpsSharedBlock (saltysd_core/source/NX-FPS-Common.hpp) is packed, so these
 * offsets are exact; several of its fields are unaligned, which is why the ones
 * that are not a byte go through memcpy. SaltyNX's own loop
 * (saltysd_proc/source/main.c, updateRefreshRateForApp) is what applies the
 * rate: FPSlocked in handheld, FPSlockedDocked docked. */
#define NX_FPS_MAGIC 0x00465053u
#define NX_FPS_HANDHELD_OFFSET 10u
#define NX_FPS_DOCKED_OFFSET 164u
#define NX_FPS_SET_BUFFERS_OFFSET 56u   /* the last field the block scan needs */
#define SESSION_COUNT_LIMIT 32u
#define POLL_INTERVAL_NS 250000000ull      /* the policy runs four times a second */
#define QUERY_INTERVAL_NS 1000000000ull    /* IPC: once a second at most */
#define STALE_NS 1000000000ull             /* a second without a frame: hold tight */
#define FOLLOW_GRACE_NS 3000000000ull      /* how long a panel may take to change mode */
#define MODE_REFRESH_NS 1000000000ull      /* how often the console's mode is asked for */

/* Two threads touch this file's state: the rate thread and whichever thread
 * presents. Every field one of them writes and the other reads is accessed
 * through __atomic_*, and nothing here is plain `volatile` -- mixing the two
 * says less than either on its own. The fields only one thread touches are
 * named where they are declared. */
static int s_enabled;            /* configure() <-> poller */
static int s_thread_created;     /* configure()/shutdown(), both on the caller's thread */
static int s_shutdown;           /* configure()/shutdown() <-> poller */
static int s_in_publish;         /* poller sets it, configure()/shutdown() wait on it */
static int s_shared_mapped;      /* poller maps, configure()/shutdown() read */
static int s_status;             /* poller writes, the runtime reads as the game ends */
static uint32_t s_service_rate;  /* poller */
static unsigned s_published_target, s_measured_fps, s_panel_rate;   /* poller writes, report() reads */
static uint64_t s_next_query_ns, s_next_poll_ns;                    /* poller only */
static uint64_t s_last_frame_ns; /* present thread writes, poller reads */
/* One of the two bytes the wanted rate goes into, and what the bridge knows
 * about it: what was there before it wrote, what it wrote, and whether the
 * player's own FPSLocker target has taken it since. */
struct nxfps_field
{
    size_t offset;
    uint8_t saved;
    uint8_t written;
    int saved_valid;
    int owned;
};

static struct nxfps_field s_handheld = { NX_FPS_HANDHELD_OFFSET, 0, 0, 0, 0 };
static struct nxfps_field s_docked = { NX_FPS_DOCKED_OFFSET, 0, 0, 0, 0 };
static unsigned s_candidate_rate;      /* poller only */
static uint64_t s_candidate_since_ns;  /* poller only */
/* What was last asked for, when, and whether the panel was seen to move to it:
 * the one way the log can say that FPSLocker's own display sync is off, which
 * is the single most likely reason for a rate that is asked for and ignored. */
static unsigned s_requested_rate;      /* poller only */
static uint64_t s_requested_at_ns;     /* poller only */
static int s_follow_checked;           /* poller only */
/* Whether the console is docked, and what was needed to find that out. Queried
 * at most once a second: omm is an IPC and this is a poll loop. */
static int s_omm_ready;                /* poller only */
static int s_omm_available;            /* poller only: written by the thread, read after it ends */
static int s_mode_docked;              /* poller only */
static uint64_t s_mode_checked_ns;     /* poller only */
static SharedMemory s_shared;
static Thread s_thread;
static uint8_t s_thread_stack[0x4000] __attribute__((aligned(0x1000)));

/* A frame's own work, in nanoseconds, as the present path reported it. Written
 * by whichever thread presents and read by the poller, so every access goes
 * through the atomics; relaxed is enough, the values carry no ordering. */
static uint64_t s_intervals[WINE_NX_DISPLAY_SYNC_SAMPLES];
static uint32_t s_interval_count;
static uint32_t s_interval_next;

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

    if (__atomic_load_n(&s_shared_mapped, __ATOMIC_ACQUIRE)) return 1;
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
    __atomic_store_n(&s_shared_mapped, 1, __ATOMIC_RELEASE);
    return 1;
}

static volatile uint8_t *find_target_field(void)
{
    uint8_t *base;
    size_t offset;

    if (!__atomic_load_n(&s_shared_mapped, __ATOMIC_ACQUIRE)) return NULL;
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
        __atomic_store_n(&s_status, WINE_NX_DISPLAY_SYNC_NO_ENDPOINT, __ATOMIC_RELAXED);
        __atomic_store_n(&s_service_rate, 0, __ATOMIC_RELAXED);
        return;
    }
    /* An endpoint that answers is an endpoint, even one too old to say what the
     * panel is doing: the status is about the decision, not the query. Only the
     * "no endpoint" verdict is cleared here -- what the last step decided about
     * the block stays until the next step decides again. */
    if (__atomic_load_n(&s_status, __ATOMIC_RELAXED) == WINE_NX_DISPLAY_SYNC_NO_ENDPOINT)
        __atomic_store_n(&s_status, WINE_NX_DISPLAY_SYNC_NOTHING, __ATOMIC_RELAXED);
    /* The rate query is for the log and for the first guess; a build of SaltyNX
     * without it can still serve the shared block, and the policy starts from
     * the measured rate alone then. */
    if (send_query(session, SALTYSD_GET_DISPLAY_REFRESH_RATE, 32, response, 3, NULL) &&
        response[2] >= 20 && response[2] <= 240)
    {
        /* The rate sits three words in, as NetherSX2 reads it. */
        __atomic_store_n(&s_service_rate, (uint32_t)response[2], __ATOMIC_RELAXED);
        if (s_requested_rate && !s_follow_checked && now > s_requested_at_ns + FOLLOW_GRACE_NS &&
            (uint32_t)response[2] != s_requested_rate)
        {
            /* SaltyNX applies the rate itself (saltysd_proc's
             * updateRefreshRateForApp), but only with FPSLocker's display sync
             * on. Three seconds is well past a mode change. */
            char text[160];

            s_follow_checked = 1;
            snprintf(text, sizeof(text),
                     "[SYNC] the panel is still at %u Hz though %u Hz was asked for: turn on "
                     "display sync in FPSLocker", (unsigned)response[2], s_requested_rate);
            log_line(text);
        }
    }
    if (!__atomic_load_n(&s_shared_mapped, __ATOMIC_ACQUIRE)) map_shared(session);
    svcCloseHandle(session);
}

static void clear_samples(void)
{
    uint32_t i;

    for (i = 0; i < WINE_NX_DISPLAY_SYNC_SAMPLES; i++)
        __atomic_store_n(&s_intervals[i], 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_interval_count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_interval_next, 0, __ATOMIC_RELAXED);
}

/* The rate the last window of frames says the game reaches. The window is read
 * without a lock -- the writer is the present thread -- so a slot can change
 * under the read; a torn window measures as nonsense, and a nonsense rate is
 * rejected by the estimator rather than published. */
static unsigned measure(unsigned *average_fps)
{
    uint64_t samples[WINE_NX_DISPLAY_SYNC_SAMPLES];
    uint32_t count = __atomic_load_n(&s_interval_count, __ATOMIC_RELAXED);
    uint32_t next = __atomic_load_n(&s_interval_next, __ATOMIC_RELAXED);
    uint32_t i;
    struct wine_nx_display_sync_estimate estimate;

    if (average_fps) *average_fps = 0;
    if (count > WINE_NX_DISPLAY_SYNC_SAMPLES) count = WINE_NX_DISPLAY_SYNC_SAMPLES;
    /* The window is a ring: read it in the order it was written. */
    for (i = 0; i < count; i++)
        samples[i] = __atomic_load_n(&s_intervals[(next + WINE_NX_DISPLAY_SYNC_SAMPLES - count + i) %
                                                  WINE_NX_DISPLAY_SYNC_SAMPLES], __ATOMIC_RELAXED);
    estimate = wine_nx_display_sync_estimate(samples, count, 1000000000ull,
                                             WINE_NX_DISPLAY_SYNC_MIN_SAMPLES);
    if (average_fps) *average_fps = estimate.average_fps;
    __atomic_store_n(&s_measured_fps, estimate.upper_fps, __ATOMIC_RELAXED);
    return estimate.upper_fps;
}

/* The block's byte for the mode the console is in. Docked uses its own field:
 * SaltyNX reads FPSlockedDocked there, and a rate written into the handheld
 * byte would be silently ignored. */
static int console_is_docked(uint64_t now)
{
    OmmOperationMode mode;

    if (s_mode_checked_ns && now >= s_mode_checked_ns && now < s_mode_checked_ns + MODE_REFRESH_NS)
        return s_mode_docked;
    s_mode_checked_ns = now ? now : 1;
    if (!s_omm_ready)
    {
        /* omm answers for the console as it is now. libnx's cached applet value
         * only follows an applet message, and this program's loop is Wine's, so
         * it would miss a dock in the middle of a game. */
        s_omm_available = R_SUCCEEDED(ommInitialize());
        s_omm_ready = 1;
    }
    if (s_omm_available && R_SUCCEEDED(ommGetOperationMode(&mode)))
        s_mode_docked = mode == OmmOperationMode_Console;
    else
        /* Without omm: the cached applet value, plus the one thing the panel
         * rate cannot do in handheld, which is run above 60 Hz. */
        s_mode_docked = appletGetOperationMode() == AppletOperationMode_Console ||
                        s_service_rate > WINE_NX_DISPLAY_SYNC_MAX_RATE;
    return s_mode_docked;
}

static uint8_t field_read(volatile uint8_t *block, const struct nxfps_field *field)
{
    return __atomic_load_n(block + field->offset, __ATOMIC_ACQUIRE);
}

/* 1 when the byte may be written. FPSLocker writes a non-zero target of its own
 * when the player chooses a rate, and that choice wins: the bridge only fills a
 * byte nobody is using, and stops as soon as someone else takes it. Zero is
 * FPSLocker's documented "auto", which hands the byte back. */
static int field_is_ours(volatile uint8_t *block, struct nxfps_field *field)
{
    uint8_t current = field_read(block, field);

    if (!current || current == field->written)
    {
        field->owned = 0;
        return 1;
    }
    field->owned = 1;
    return 0;
}

/* Put value in the byte unless someone changed it in between, and remember what
 * was there first so it can be put back. */
static int field_write(volatile uint8_t *block, struct nxfps_field *field, uint8_t value)
{
    uint8_t expected = field_read(block, field), before = expected;

    if (!__atomic_compare_exchange_n(block + field->offset, &expected, value, false,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
        /* Someone wrote it between the read and the write: theirs. Nothing of
         * the bridge's is in the byte, so there is nothing to remember either. */
        field->owned = expected != 0;
        return 0;
    }
    /* What was there before the first write of this session, and only that: the
     * value the byte is put back to when the game's rate changes again. */
    if (!field->saved_valid)
    {
        field->saved = before;
        field->saved_valid = 1;
    }
    field->written = value;
    return 1;
}

/* Hand the byte back, but only if it still says what the bridge put there. */
static void field_restore(volatile uint8_t *block, struct nxfps_field *field)
{
    uint8_t expected = field->written;

    if (!field->saved_valid || field->owned || !field->written) return;
    __atomic_compare_exchange_n(block + field->offset, &expected, field->saved, false,
                                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    field->written = 0;
    field->saved_valid = 0;
}

static void fields_reset(void)
{
    s_handheld.saved = s_handheld.written = 0;
    s_handheld.saved_valid = s_handheld.owned = 0;
    s_docked.saved = s_docked.written = 0;
    s_docked.saved_valid = s_docked.owned = 0;
}

/* Hand both bytes back: called when the bridge is turned off, and as the program
 * ends. The one of the two the console is not using is restored too, so a
 * console docked in the middle of a game is not left with a rate. */
static void restore_fields(void)
{
    volatile uint8_t *block;

    block = find_target_field();
    if (!block) return;
    field_restore(block, &s_handheld);
    field_restore(block, &s_docked);
}

/* The game no longer needs the rate it was given: the byte goes back to 60,
 * which is the rate SaltyNX applies (and what FPSLocker's own reset writes).
 * Restoring the old value would not raise the panel -- SaltyNX keeps the last
 * rate it was given -- so this is a write, not a hand-back; the old value is
 * still what the session ends on. */
static void release_rate(volatile uint8_t *block, struct nxfps_field *field, unsigned rate)
{
    if (field->written && field->written < WINE_NX_DISPLAY_SYNC_MAX_RATE && field_is_ours(block, field) &&
        field_write(block, field, (uint8_t)WINE_NX_DISPLAY_SYNC_MAX_RATE))
        __atomic_store_n(&s_published_target, WINE_NX_DISPLAY_SYNC_MAX_RATE, __ATOMIC_RELAXED);
    __atomic_store_n(&s_panel_rate, rate, __ATOMIC_RELAXED);
    __atomic_store_n(&s_status, WINE_NX_DISPLAY_SYNC_FOLLOWING, __ATOMIC_RELAXED);
    s_requested_rate = 0;
    s_follow_checked = 0;
    s_candidate_since_ns = 0;
}

static void publish(uint64_t now)
{
    volatile uint8_t *block;
    struct nxfps_field *field, *other;
    unsigned wanted, average = 0, measured, service_rate, published;
    uint64_t stale_before = now > STALE_NS ? now - STALE_NS : 0, last_frame;
    uint8_t current;
    int docked;

    /* No endpoint, nothing to go through: the rate query already said so, and a
     * "no block" would only hide it. */
    if (__atomic_load_n(&s_status, __ATOMIC_RELAXED) == WINE_NX_DISPLAY_SYNC_NO_ENDPOINT) return;
    block = find_target_field();
    if (!block)
    {
        __atomic_store_n(&s_status, WINE_NX_DISPLAY_SYNC_NO_BLOCK, __ATOMIC_RELAXED);
        return;
    }
    docked = console_is_docked(now);
    field = docked ? &s_docked : &s_handheld;
    other = docked ? &s_handheld : &s_docked;
    /* Dock or undock mid-game: the byte of the other mode is not being read
     * any more, so it goes back to what it was. */
    if (other->written) field_restore(block, other);

    last_frame = __atomic_load_n(&s_last_frame_ns, __ATOMIC_RELAXED);
    if (!last_frame || last_frame < stale_before)
    {
        /* No frame for a second: a load screen or a paused game is not a
         * measurement, and the panel stays where it is. */
        clear_samples();
        return;
    }
    measured = measure(&average);
    if (!measured)
    {
        __atomic_store_n(&s_status, WINE_NX_DISPLAY_SYNC_MEASURING, __ATOMIC_RELAXED);
        return;
    }
    service_rate = __atomic_load_n(&s_service_rate, __ATOMIC_RELAXED);
    published = __atomic_load_n(&s_published_target, __ATOMIC_RELAXED);
    wanted = wine_nx_display_sync_next_rate(published ? published : service_rate, measured);
    /* 60 is where the panel already is, and a docked one may be faster still
     * (up to 120 Hz): a game that reaches 60 is not asking for anything less,
     * and writing 60 would be a downgrade. This is also the way back -- a game
     * that was slow and has caught up is put back on 60 instead of being left
     * on the low rate for the rest of the session. The test is on the
     * measurement, not on wanted: right after that, the last rate the service
     * reported is still low, and stepping up from it would put the panel down
     * again. */
    if (wine_nx_display_sync_legal_rate(measured) >= WINE_NX_DISPLAY_SYNC_MAX_RATE)
    {
        release_rate(block, field, service_rate ? service_rate : WINE_NX_DISPLAY_SYNC_MAX_RATE);
        return;
    }
    if (!field->written && service_rate && wanted == service_rate)
    {
        /* The panel is already where the game is, and the byte has not been
         * touched: leave it alone so whatever FPSLocker has stays. A game at
         * 60 fps or better never moves the panel. */
        __atomic_store_n(&s_panel_rate, service_rate, __ATOMIC_RELAXED);
        __atomic_store_n(&s_status, WINE_NX_DISPLAY_SYNC_FOLLOWING, __ATOMIC_RELAXED);
        return;
    }
    if (!field_is_ours(block, field))
    {
        __atomic_store_n(&s_status, WINE_NX_DISPLAY_SYNC_FPSLOCKER, __ATOMIC_RELAXED);
        return;
    }
    current = field_read(block, field);
    /* The byte already says what it should, and it is ours: nothing to do. A
     * byte someone cleared is not that -- the rate asked for is re-asserted,
     * after the same wait, so a player who clears FPSLocker's target gets the
     * panel back on the game. */
    if (wanted == field->written && current == field->written)
    {
        __atomic_store_n(&s_status, WINE_NX_DISPLAY_SYNC_FOLLOWING, __ATOMIC_RELAXED);
        return;
    }
    if (!wine_nx_display_sync_stable(wanted, &s_candidate_rate, &s_candidate_since_ns, now,
                                     (uint64_t)WINE_NX_DISPLAY_SYNC_STABLE_MS * 1000000ull))
        return;

    if (!field_write(block, field, (uint8_t)wanted))
    {
        __atomic_store_n(&s_status, WINE_NX_DISPLAY_SYNC_FPSLOCKER, __ATOMIC_RELAXED);
        return;
    }
    __atomic_store_n(&s_published_target, wanted, __ATOMIC_RELAXED);
    __atomic_store_n(&s_panel_rate, wanted, __ATOMIC_RELAXED);
    s_requested_rate = wanted;
    s_requested_at_ns = now;
    s_follow_checked = 0;
    s_candidate_since_ns = 0;
    __atomic_store_n(&s_status, WINE_NX_DISPLAY_SYNC_FOLLOWING, __ATOMIC_RELAXED);
    {
        char text[96];

        snprintf(text, sizeof(text), "[SYNC] asked for %u Hz (%s), game at %u fps", wanted,
                 field == &s_docked ? "docked" : "handheld", measured);
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
        /* Turning the bridge off restores FPSlocked from the other thread; the
         * flag is what it waits for, so a write made here cannot land after
         * the restore. */
        __atomic_store_n(&s_in_publish, 1, __ATOMIC_RELEASE);
        publish(now);
        __atomic_store_n(&s_in_publish, 0, __ATOMIC_RELEASE);
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
}

/* Wait, but not forever, for the poller to step out of publish(). Called from
 * the thread that is turning the bridge off, never from the poller. */
static void wait_out_of_publish(void)
{
    int waits;

    for (waits = 0; waits < 200 && __atomic_load_n(&s_in_publish, __ATOMIC_ACQUIRE); waits++)
        svcSleepThread(1000000);
}

/* Everything one game session leaves behind: the value written, the value it
 * replaced, the measurement window, and what the last step decided. */
static void reset_session(void)
{
    fields_reset();
    __atomic_store_n(&s_published_target, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_panel_rate, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_measured_fps, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s_status, WINE_NX_DISPLAY_SYNC_NOTHING, __ATOMIC_RELAXED);   /* off is nothing yet, not what the last session said */
    s_requested_rate = 0;
    s_requested_at_ns = 0;
    s_follow_checked = 0;
    s_mode_checked_ns = 0;
    s_mode_docked = 0;
    s_prefer_legacy = 0;    /* the next game asks the fork's port again */
    s_logged_missing = 0;
    clear_samples();
}

void wine_nx_display_sync_configure(int enabled)
{
    int was_on = __atomic_load_n(&s_enabled, __ATOMIC_ACQUIRE);

    if (!enabled)
    {
        __atomic_store_n(&s_enabled, 0, __ATOMIC_RELEASE);
        wait_out_of_publish();
        /* Hand FPSlocked back to whoever had it before. */
        if (__atomic_load_n(&s_shared_mapped, __ATOMIC_ACQUIRE)) restore_fields();
        reset_session();
        if (was_on) log_line("[SYNC] panel rate following the game: off");
        return;
    }
    if (__atomic_load_n(&s_enabled, __ATOMIC_ACQUIRE)) return;
    __atomic_store_n(&s_shutdown, 0, __ATOMIC_RELEASE);
    clear_samples();
    s_next_query_ns = 0;
    s_next_poll_ns = 0;
    if (!s_thread_created)
    {
        /* Below the game's threads and on any core, like the log reader. A
         * failed create leaves nothing behind (libnx unwinds its own mapping),
         * a failed start leaves a mapped thread that has to be closed. */
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
        s_thread_created = 1;
    }
    __atomic_store_n(&s_enabled, 1, __ATOMIC_RELEASE);
    log_line("[SYNC] panel rate following the game: on");
}

void wine_nx_display_sync_note_frame(uint64_t busy_ns)
{
    uint64_t now;

    if (!__atomic_load_n(&s_enabled, __ATOMIC_ACQUIRE)) return;
    now = now_ns();
    if (!now) return;
    __atomic_store_n(&s_last_frame_ns, now, __ATOMIC_RELAXED);
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

const char *wine_nx_display_sync_status_text(int status)
{
    switch (status)
    {
    case WINE_NX_DISPLAY_SYNC_NOTHING: return "nothing asked yet";
    case WINE_NX_DISPLAY_SYNC_MEASURING: return "measuring";
    case WINE_NX_DISPLAY_SYNC_FOLLOWING: return "the panel follows the game";
    case WINE_NX_DISPLAY_SYNC_FPSLOCKER: return "FPSLocker is setting the rate";
    case WINE_NX_DISPLAY_SYNC_NO_BLOCK: return "no FPSLocker block in the shared memory";
    case WINE_NX_DISPLAY_SYNC_NO_ENDPOINT: return "no SaltySD endpoint";
    default: return "unknown";
    }
}

void wine_nx_display_sync_report(struct wine_nx_display_sync_report *report)
{
    if (!report) return;
    report->measured_fps = __atomic_load_n(&s_measured_fps, __ATOMIC_RELAXED);
    report->panel_rate = __atomic_load_n(&s_panel_rate, __ATOMIC_RELAXED);
    report->service_rate = __atomic_load_n(&s_service_rate, __ATOMIC_RELAXED);
    report->status = __atomic_load_n(&s_status, __ATOMIC_RELAXED);
}

/* Called once as the program ends: the thread goes, the block is closed and
 * FPSlocked is left as it was found. */
void wine_nx_display_sync_shutdown(void)
{
    __atomic_store_n(&s_shutdown, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&s_enabled, 0, __ATOMIC_RELEASE);
    if (s_thread_created)
    {
        /* Closed only once it really has exited: threadClose takes the stack
         * mirror and the handle away, and a thread that is still running on
         * them is a crash. Its loop notices the quit within a step. */
        if (R_SUCCEEDED(waitSingle(waiterForThread(&s_thread), 3000000000ULL)))
            threadClose(&s_thread);
        else
            log_line("[SYNC] the rate thread did not end; its stack stays lent out");
        s_thread_created = 0;
    }
    wait_out_of_publish();
    if (s_omm_available)
    {
        /* The poller is gone by now, so nothing can be using the session. */
        ommExit();
        s_omm_available = 0;
        s_omm_ready = 0;
    }
    if (__atomic_load_n(&s_shared_mapped, __ATOMIC_ACQUIRE))
    {
        restore_fields();
        shmemClose(&s_shared);
        memset(&s_shared, 0, sizeof(s_shared));
        __atomic_store_n(&s_shared_mapped, 0, __ATOMIC_RELEASE);
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
const char *wine_nx_display_sync_status_text(int status) { (void)status; return "not on the console"; }

#endif
