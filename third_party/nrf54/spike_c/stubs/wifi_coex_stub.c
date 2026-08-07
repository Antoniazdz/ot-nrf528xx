/*
 * Variant C spike: wifi coexistence stubs (coex disabled).
 */

#include <stdbool.h>
#include <stdint.h>

#include <nrf_802154.h>

bool nrf_802154_wifi_coex_is_enabled(void)
{
    return false;
}

void nrf_802154_wifi_coex_init(void)
{
}

void nrf_802154_wifi_coex_uninit(void)
{
}

void nrf_802154_wifi_coex_prio_request(uint8_t prio)
{
    (void)prio;
}

void nrf_802154_wifi_coex_granted(void)
{
}

void nrf_802154_wifi_coex_denied(void)
{
}

void nrf_802154_wifi_coex_on_raal_timeslot_started(void)
{
}

void nrf_802154_wifi_coex_on_rsch_continuous_ended(void)
{
}
