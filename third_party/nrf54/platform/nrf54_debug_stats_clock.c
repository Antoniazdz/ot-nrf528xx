/*
 * Copyright (c) 2026, The OpenThread Authors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * HFCLK / GRTC always-on verification samples for debug stats.
 */

#include "nrf54_debug_stats.h"

#include "nrf_802154_clock.h"

#include <nrfx_grtc.h>

void nrf54DebugStatsClockSample(void)
{
    g_nrf54_debug_stats.grtc_clock_sample_checks++;

    if (nrfx_grtc_init_check())
    {
        const bool active = nrfx_grtc_active_request_check();
        const bool ready  = nrfx_grtc_ready_check();

        g_nrf54_debug_stats.last_grtc_active_request = active ? 1U : 0U;
        g_nrf54_debug_stats.last_grtc_ready          = ready ? 1U : 0U;

        if (!active)
        {
            g_nrf54_debug_stats.grtc_not_active_request_samples++;
        }

        if (!ready)
        {
            g_nrf54_debug_stats.grtc_not_ready_samples++;
        }
    }

    if (!nrf_802154_clock_hfclk_is_running())
    {
        g_nrf54_debug_stats.hfclk_not_running_samples++;
    }
}
