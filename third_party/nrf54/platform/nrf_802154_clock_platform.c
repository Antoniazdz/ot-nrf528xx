/*
 * Copyright (c) 2026, The OpenThread Authors.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Bare-metal nRF54L15 implementation of the nRF 802.15.4 clock platform API.
 * Adapted from Nordic nrf_802154_clock_sdk.c / nrf_802154_clock_nodrv.c using
 * nrfx 4.x split clock drivers.
 *
 * nRF54L15 application core: LFCLK via nrfx_clock_lfclk, HFCLK API backed by XO
 * (nrfx_clock_xo) — there is no legacy nrfx_clock_hfclk on this SoC.
 */

#include "nrf_802154_clock.h"

#include <stdbool.h>
#include <stdint.h>

#include <nrfx.h>
#include <nrfx_clock_lfclk.h>

#if NRF_CLOCK_HAS_HFCLK
#include <nrfx_clock_hfclk.h>
#elif NRF_CLOCK_HAS_XO
#include <nrfx_clock_xo.h>
#endif

static uint8_t mLfclkUsers;
static uint8_t mHfclkUsers;

static void lfclk_evt_handler(nrfx_clock_lfclk_evt_type_t event)
{
    if (event == NRFX_CLOCK_LFCLK_EVT_LFCLK_STARTED)
    {
        nrf_802154_clock_lfclk_ready();
    }
}

#if NRF_CLOCK_HAS_HFCLK

static void hfclk_evt_handler(void)
{
    nrf_802154_clock_hfclk_ready();
}

static bool hfclk_running_check(void)
{
    nrf_clock_hfclk_t clk_src;

    return nrfx_clock_hfclk_running_check(&clk_src);
}

static void hfclk_drv_init(void)
{
    if (!nrfx_clock_hfclk_init_check())
    {
        (void)nrfx_clock_hfclk_init(hfclk_evt_handler);
    }
}

static void hfclk_drv_uninit(void)
{
    if (nrfx_clock_hfclk_init_check())
    {
        nrfx_clock_hfclk_uninit();
    }
}

static void hfclk_drv_start(void)
{
    nrfx_clock_hfclk_start();
}

static void hfclk_drv_stop(void)
{
    nrfx_clock_hfclk_stop();
}

static void hfclk_irq_handler(void)
{
    nrfx_clock_hfclk_irq_handler();
}

#elif NRF_CLOCK_HAS_XO

static void xo_evt_handler(nrfx_clock_xo_event_type_t event)
{
    if (event == NRFX_CLOCK_XO_EVT_HFCLK_STARTED)
    {
        nrf_802154_clock_hfclk_ready();
    }
}

static bool hfclk_running_check(void)
{
    nrf_clock_hfclk_t clk_src;

    return nrfx_clock_xo_running_check(&clk_src);
}

static void hfclk_drv_init(void)
{
    if (!nrfx_clock_xo_init_check())
    {
        (void)nrfx_clock_xo_init(xo_evt_handler);
    }
}

static void hfclk_drv_uninit(void)
{
    if (nrfx_clock_xo_init_check())
    {
        nrfx_clock_xo_uninit();
    }
}

static void hfclk_drv_start(void)
{
    nrfx_clock_xo_start();
}

static void hfclk_drv_stop(void)
{
    nrfx_clock_xo_stop();
}

static void hfclk_irq_handler(void)
{
    nrfx_clock_xo_irq_handler();
}

#else
#error "This SoC has neither HFCLK nor XO clock support."
#endif

void nrf_802154_clock_init(void)
{
    if (!nrfx_clock_lfclk_init_check())
    {
        (void)nrfx_clock_lfclk_init(lfclk_evt_handler);
    }

    hfclk_drv_init();

    NVIC_SetPriority(CLOCK_POWER_IRQn, NRFX_CLOCK_DEFAULT_CONFIG_IRQ_PRIORITY);
    NVIC_ClearPendingIRQ(CLOCK_POWER_IRQn);
    NVIC_EnableIRQ(CLOCK_POWER_IRQn);
}

void nrf_802154_clock_deinit(void)
{
    NVIC_DisableIRQ(CLOCK_POWER_IRQn);
    NVIC_ClearPendingIRQ(CLOCK_POWER_IRQn);

    mLfclkUsers = 0;
    mHfclkUsers = 0;

    hfclk_drv_uninit();

    if (nrfx_clock_lfclk_init_check())
    {
        nrfx_clock_lfclk_uninit();
    }
}

void nrf_802154_clock_hfclk_start(void)
{
    if (mHfclkUsers == UINT8_MAX) { return; }
    if (mHfclkUsers++ == 0)
    {
        if (hfclk_running_check())
        {
            nrf_802154_clock_hfclk_ready();
        }
        else
        {
            hfclk_drv_start();
        }
    }
    else if (hfclk_running_check())
    {
        nrf_802154_clock_hfclk_ready();
    }
}

void nrf_802154_clock_hfclk_stop(void)
{
    
    if ((mHfclkUsers > 0) && (--mHfclkUsers == 0))
    {
        if (hfclk_running_check())
        {
            hfclk_drv_stop();
        }
    }
}

bool nrf_802154_clock_hfclk_is_running(void)
{
    return hfclk_running_check();
}

void nrf_802154_clock_lfclk_start(void)
{
    if (mLfclkUsers++ == 0)
    {
        if (nrfx_clock_lfclk_running_check(NULL))
        {
            nrf_802154_clock_lfclk_ready();
        }
        else
        {
            nrfx_clock_lfclk_start();
        }
    }
}

void nrf_802154_clock_lfclk_stop(void)
{
    if ((mLfclkUsers > 0) && (--mLfclkUsers == 0))
    {
        nrfx_clock_lfclk_stop();
    }
}

bool nrf_802154_clock_lfclk_is_running(void)
{
    return nrfx_clock_lfclk_running_check(NULL);
}

void CLOCK_POWER_IRQHandler(void)
{
    hfclk_irq_handler();
    nrfx_clock_lfclk_irq_handler();
}
