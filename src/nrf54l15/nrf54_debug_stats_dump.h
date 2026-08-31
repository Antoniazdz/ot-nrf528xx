/*
 * Copyright (c) 2026, The OpenThread Authors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef NRF54_DEBUG_STATS_DUMP_H_
#define NRF54_DEBUG_STATS_DUMP_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Emit one line of text (e.g. to UART via diag or CLI). */
typedef void (*Nrf54StatsEmitLineFn)(void *aContext, const char *aLine);

/** Log human-readable CSL/radio counters to RTT (otPlatLog). */
void nrf54DebugStatsDumpSummary(void);

/** Same fields as summary, each line passed to @p aEmit (and still logged to RTT). */
void nrf54DebugStatsDumpSummaryEmit(Nrf54StatsEmitLineFn aEmit, void *aContext);

/** CSL/DRX handoff counters only (~20 lines) for automated tests. */
void nrf54DebugStatsDumpHandoffEmit(Nrf54StatsEmitLineFn aEmit, void *aContext);

/** Log entire struct as hex words (offset + 4 x uint32 per line) to RTT. */
void nrf54DebugStatsDumpRaw(void);

/** Raw hex dump via @p aEmit and RTT. */
void nrf54DebugStatsDumpRawEmit(Nrf54StatsEmitLineFn aEmit, void *aContext);

/** Zero g_nrf54_debug_stats. */
void nrf54DebugStatsClear(void);

#ifdef __cplusplus
}
#endif

#endif /* NRF54_DEBUG_STATS_DUMP_H_ */
