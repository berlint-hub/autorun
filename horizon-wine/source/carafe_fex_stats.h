#ifndef CARAFE_FEX_STATS_H
#define CARAFE_FEX_STATS_H

#include <switch.h>

void carafeFexStatsReport(void);
/* The ticks FEX has spent translating code since it started, on the thread
 * with the Windows thread id tid and on every thread it counts. 0 without
 * FEX's counters, with both set to 0. */
int carafeFexStatsJitTicks(u32 tid, u64* thread, u64* all);

#endif
