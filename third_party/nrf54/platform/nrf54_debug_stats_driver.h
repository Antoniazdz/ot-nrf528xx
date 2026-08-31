/*
 * Copyright (c) 2026, The OpenThread Authors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Increment helpers for nRF 802.15.4 driver (NRF54_DEBUG_STATS builds only).
 */

#ifndef NRF54_DEBUG_STATS_DRIVER_H_
#define NRF54_DEBUG_STATS_DRIVER_H_

#ifdef NRF54_DEBUG_STATS

#include "nrf54_debug_stats.h"

#define NRF54_STAT_INC(_field) (void)(++g_nrf54_debug_stats._field)
#define NRF54_STAT_SET(_field, _value) (g_nrf54_debug_stats._field = (_value))

#else

#define NRF54_STAT_INC(_field) ((void)0)
#define NRF54_STAT_SET(_field, _value) ((void)0)

#endif

#endif /* NRF54_DEBUG_STATS_DRIVER_H_ */
