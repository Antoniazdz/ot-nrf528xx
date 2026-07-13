/*
 * Copyright (c) 2026, The OpenThread Authors.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Minimal POC stub for nrf_802154 SL lptimer platform layer.
 *
 * Used when NRF54_POC_MINIMAL_TIMERS=ON (default). Does not allocate GRTC compare
 * channels CC2/CC3/CC8 or configure DPPI — alarm_nrf54.c owns CC0/CC1 and
 * nrfx_grtc_init(). Full implementation remains in:
 *   - nrf_802154_platform_sl_lptimer.c
 *   - nrf_802154_platform_sl_lptimer_grtc_hw_task.c
 *
 * Switch back: cmake -DNRF54_POC_MINIMAL_TIMERS=OFF
 */

#include "platform/nrf_802154_platform_sl_lptimer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <nrfx_grtc.h>

#ifndef OT_GRTC_US_PER_TICK
#define OT_GRTC_US_PER_TICK 1ULL
#endif

static uint32_t m_critical_section_cnt;

void nrf_802154_platform_sl_lp_timer_init(void)
{
    m_critical_section_cnt = 0;
    assert(nrfx_grtc_init_check());
}

void nrf_802154_platform_sl_lp_timer_deinit(void)
{
    m_critical_section_cnt = 0;
}

uint64_t nrf_802154_platform_sl_lptimer_current_lpticks_get(void)
{
    if (!nrfx_grtc_init_check())
    {
        return 0;
    }

    return nrfx_grtc_syscounter_get();
}

uint64_t nrf_802154_platform_sl_lptimer_us_to_lpticks_convert(uint64_t us, bool round_up)
{
    uint64_t ticks = us / OT_GRTC_US_PER_TICK;

    if (round_up && (ticks * OT_GRTC_US_PER_TICK < us))
    {
        ticks++;
    }

    return ticks;
}

uint64_t nrf_802154_platform_sl_lptimer_lpticks_to_us_convert(uint64_t lpticks)
{
    return lpticks * OT_GRTC_US_PER_TICK;
}

void nrf_802154_platform_sl_lptimer_schedule_at(uint64_t fire_lpticks)
{
    (void)fire_lpticks;
}

void nrf_802154_platform_sl_lptimer_disable(void)
{
}

void nrf_802154_platform_sl_lptimer_critical_section_enter(void)
{
    m_critical_section_cnt++;
}

void nrf_802154_platform_sl_lptimer_critical_section_exit(void)
{
    assert(m_critical_section_cnt > 0U);
    m_critical_section_cnt--;
}

uint32_t nrf_802154_platform_sl_lptimer_granularity_get(void)
{
    return (uint32_t)OT_GRTC_US_PER_TICK;
}

void nrf_802154_platform_sl_lptimer_sync_schedule_now(void)
{
}

void nrf_802154_platform_sl_lptimer_sync_schedule_at(uint64_t fire_lpticks)
{
    (void)fire_lpticks;
}

void nrf_802154_platform_sl_lptimer_sync_abort(void)
{
}

uint32_t nrf_802154_platform_sl_lptimer_sync_event_get(void)
{
    return 0U;
}

uint64_t nrf_802154_platform_sl_lptimer_sync_lpticks_get(void)
{
    return 0;
}

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_prepare(uint64_t fire_lpticks,
                                                                                        uint32_t ppi_channel)
{
    (void)fire_lpticks;
    (void)ppi_channel;

    return NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE;
}

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_cleanup(void)
{
    return NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE;
}

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_update_ppi(uint32_t ppi_channel)
{
    (void)ppi_channel;

    return NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE;
}
