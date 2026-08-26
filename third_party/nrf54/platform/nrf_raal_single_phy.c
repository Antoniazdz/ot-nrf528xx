/*
 * Variant C: RAAL single-PHY stub (replaces MPSL REM arbiter).
 *
 * Ensures HFXO is running before nrf_raal_timeslot_started(), matching RSCH
 * delayed-timeslot precondition timing (see PREC_HFXO_STARTUP_TIME_WORST on nRF54L).
 */

#include <stdbool.h>
#include <stdint.h>

#include <nrfx.h>

#include "nrf_802154_clock.h"
#include "nrf_802154_utils.h"

/* Matches PREC_HFXO_STARTUP_TIME_WORST in nrf_802154_rsch.c for NRF54L_SERIES. */
#define RAAL_HFCLK_WAIT_US 2000U

__attribute__((weak)) void nrf_raal_timeslot_started(void)
{
}

__attribute__((weak)) void nrf_raal_timeslot_ended(void)
{
}

static bool m_continuous;

static bool hfclk_wait_until_running(void)
{
    nrf_802154_clock_hfclk_start();

    if (nrf_802154_clock_hfclk_is_running())
    {
        return true;
    }

    for (uint32_t waited = 0; waited < RAAL_HFCLK_WAIT_US; waited++)
    {
        nrf_802154_delay_us(1);
        if (nrf_802154_clock_hfclk_is_running())
        {
            return true;
        }
    }

    return nrf_802154_clock_hfclk_is_running();
}

static void raal_grant_timeslot(void)
{
    if (hfclk_wait_until_running())
    {
        nrf_raal_timeslot_started();
    }
}

void nrf_raal_init(void)
{
    m_continuous = false;
}

void nrf_raal_uninit(void)
{
    m_continuous = false;
}

void nrf_raal_continuous_mode_enter(void)
{
    m_continuous = true;
    raal_grant_timeslot();
}

void nrf_raal_continuous_mode_exit(void)
{
    m_continuous = false;
}

void nrf_raal_continuous_ended(void)
{
}

bool nrf_raal_timeslot_request_with_prio(uint32_t length_us, uint8_t prio)
{
    (void)length_us;
    (void)prio;

    if (!m_continuous)
    {
        raal_grant_timeslot();
    }

    return true;
}

uint32_t nrf_raal_timeslot_us_left_get(void)
{
    return UINT32_MAX;
}
