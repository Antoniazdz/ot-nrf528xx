/*
 * Variant C spike: RAAL single-PHY stub (replaces MPSL REM arbiter).
 */

#include <stdbool.h>
#include <stdint.h>

#include <nrf_802154.h>

__attribute__((weak)) void nrf_raal_timeslot_started(void)
{
}

__attribute__((weak)) void nrf_raal_timeslot_ended(void)
{
}

static bool m_continuous;

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
    nrf_raal_timeslot_started();
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
        nrf_raal_timeslot_started();
    }

    return true;
}

uint32_t nrf_raal_timeslot_us_left_get(void)
{
    return UINT32_MAX;
}
