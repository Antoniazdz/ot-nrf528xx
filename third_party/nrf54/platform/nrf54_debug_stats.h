/*
 * Copyright (c) 2026, The OpenThread Authors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Volatile debug counters for nRF54 RCP bring-up (session d26209).
 */

#ifndef NRF54_DEBUG_STATS_H_
#define NRF54_DEBUG_STATS_H_

#include <stdint.h>

typedef struct
{
    uint32_t cc2_timer_fires;
    uint32_t hw_task_prepare_ok;
    uint32_t hw_task_prepare_fail;
    uint32_t rsch_dly_start;
    uint32_t rsch_dly_start_no_hfclk;
    uint32_t rsch_all_prec_update;
    uint32_t rsch_notify_core;
    uint32_t tx_fail_busy_channel;
    uint32_t hfclk_ready_calls;
} nrf54_debug_stats_t;

extern volatile nrf54_debug_stats_t g_nrf54_debug_stats;

#endif /* NRF54_DEBUG_STATS_H_ */
