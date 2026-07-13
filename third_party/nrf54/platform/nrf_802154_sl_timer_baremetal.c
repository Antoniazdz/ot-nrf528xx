/*
 * Copyright (c) 2026, The OpenThread Authors.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Bare-metal nRF54L15 implementation of nrf_802154_sl_timer (802.15.4 driver
 * timer scheduler). Replaces the Zephyr stub in
 * sl/sl_opensource/src/nrf_802154_sl_timer.c when wired in CMake.
 *
 * Step 1: module init, current time, timer instance init/deinit, timer_coord stubs,
 *         and link stubs for add/remove/handler (scheduler in later steps).
 *
 * POC (NRF54_POC_MINIMAL_TIMERS=ON): platform is nrf_802154_platform_sl_lptimer_stub.c;
 * sl_timer_add/remove/handler are no-ops — enough for UART/Spinel bring-up.
 *
 * Full build (NRF54_POC_MINIMAL_TIMERS=OFF): platform is
 * nrf_802154_platform_sl_lptimer.c + nrf_802154_platform_sl_lptimer_grtc_hw_task.c.
 *
 * Reference: third_party/NordicSemiconductor/drivers/radio/timer_scheduler/
 *            nrf_802154_timer_sched.c (nRF52 timer_sched — same role, later steps)
 */

#include "nrf_802154_sl_timer.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform/nrf_802154_platform_sl_lptimer.h"
#include "timer/nrf_802154_timer_coord.h"

typedef struct
{
    nrf_802154_sl_timer_t *p_next;
    bool                     in_queue;
} sl_timer_priv_t;

/* -------------------------------------------------------------------------- */
/* Timer coordinator (stubs while NRF_802154_FRAME_TIMESTAMP_ENABLED=0)       */
/* Ref: sl/sl_opensource/src/nrf_802154_sl_timer.c                            */
/* -------------------------------------------------------------------------- */

void nrf_802154_timer_coord_init(void)
{
}

void nrf_802154_timer_coord_uninit(void)
{
}

void nrf_802154_timer_coord_start(void)
{
}

void nrf_802154_timer_coord_stop(void)
{
}

void nrf_802154_timer_coord_timestamp_prepare(const nrf_802154_sl_event_handle_t *p_event)
{
    (void)p_event;
}

bool nrf_802154_timer_coord_timestamp_get(uint64_t *p_timestamp)
{
    (void)p_timestamp;

    return false;
}

/* -------------------------------------------------------------------------- */
/* Module init — platform GRTC CC2/CC3/CC8 (requires nrfx_grtc_init first)    */
/* -------------------------------------------------------------------------- */

void nrf_802154_sl_timer_module_init(void)
{
    nrf_802154_platform_sl_lp_timer_init();
}

void nrf_802154_sl_timer_module_uninit(void)
{
    nrf_802154_platform_sl_lp_timer_deinit();
}

uint64_t nrf_802154_sl_timer_current_time_get(void)
{
    return nrf_802154_platform_sl_lptimer_lpticks_to_us_convert(
        nrf_802154_platform_sl_lptimer_current_lpticks_get());
}

void nrf_802154_sl_timer_init(nrf_802154_sl_timer_t *p_timer)
{
    memset(&p_timer->priv, 0, sizeof(p_timer->priv));
}

void nrf_802154_sl_timer_deinit(nrf_802154_sl_timer_t *p_timer)
{
    memset(&p_timer->priv, 0, sizeof(p_timer->priv));
}

/* -------------------------------------------------------------------------- */
/* Link stubs — full scheduler in later steps                                 */
/* -------------------------------------------------------------------------- */

void nrf_802154_sl_timer_handler(uint64_t now_lpticks)
{
    (void)now_lpticks;
}

nrf_802154_sl_timer_ret_t nrf_802154_sl_timer_add(nrf_802154_sl_timer_t *p_timer)
{
    (void)p_timer;

    return NRF_802154_SL_TIMER_RET_SUCCESS;
}

nrf_802154_sl_timer_ret_t nrf_802154_sl_timer_remove(nrf_802154_sl_timer_t *p_timer)
{
    (void)p_timer;

    return NRF_802154_SL_TIMER_RET_INACTIVE;
}

nrf_802154_sl_timer_ret_t nrf_802154_sl_timer_update_ppi(nrf_802154_sl_timer_t *p_timer, uint32_t ppi_chn)
{
    (void)p_timer;
    (void)ppi_chn;

    return NRF_802154_SL_TIMER_RET_BAD_REQUEST;
}
