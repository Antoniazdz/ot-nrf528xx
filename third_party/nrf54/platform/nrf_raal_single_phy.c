/*
 * Variant C: RAAL single-PHY arbiter (replaces the MPSL REM arbiter).
 *
 * On a single-protocol build the RADIO peripheral is never arbitrated away, so
 * every timeslot request is granted unconditionally and immediately.
 *
 * HFXO is a *separate* RSCH precondition (RSCH_PREC_HFCLK) from radio
 * arbitration (RSCH_PREC_RAAL). The SL library shipped for nRF54L does not
 * import the nrf_802154_clock_hfclk_* API, so nothing else keeps that
 * precondition satisfied for the radio. This arbiter therefore holds a single
 * HFXO reference for as long as it stays initialised instead of requesting the
 * clock on every timeslot.
 *
 * The grant path must never block: nrf_raal_timeslot_started() has no
 * subscriber in this build (the SL library does not import it either), so a
 * grant that is deferred until the clock reports ready could never be
 * delivered. Requesting the clock once up front is what makes an immediate,
 * non-blocking grant correct.
 */

#include <stdbool.h>
#include <stdint.h>

#include <nrfx.h>

#include "nrf_802154_clock.h"

__attribute__((weak)) void nrf_raal_timeslot_started(void)
{
}

__attribute__((weak)) void nrf_raal_timeslot_ended(void)
{
}

static bool m_initialized;

void nrf_raal_init(void)
{
    if (!m_initialized)
    {
        m_initialized = true;

        /* Paired with the release in nrf_raal_uninit(). */
        nrf_802154_clock_hfclk_start();
    }
}

void nrf_raal_uninit(void)
{
    if (m_initialized)
    {
        m_initialized = false;
        nrf_802154_clock_hfclk_stop();
    }
}

void nrf_raal_continuous_mode_enter(void)
{
    nrf_raal_timeslot_started();
}

void nrf_raal_continuous_mode_exit(void)
{
    /* No nrf_raal_timeslot_ended() here: that callback reports involuntary
     * preemption, and the core is the one asking to leave continuous mode. */
}

void nrf_raal_continuous_ended(void)
{
}

bool nrf_raal_timeslot_request_with_prio(uint32_t length_us, uint8_t prio)
{
    (void)length_us;
    (void)prio;

    return true;
}

uint32_t nrf_raal_timeslot_us_left_get(void)
{
    return UINT32_MAX;
}
