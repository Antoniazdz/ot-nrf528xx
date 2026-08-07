/*
 * Variant C spike: minimal timestamper platform (G2 rsch_init only).
 */

#include "platform/nrf_802154_platform_timestamper.h"

#include <stddef.h>

void nrf_802154_platform_timestamper_init(void)
{
}

void nrf_802154_platform_timestamper_cross_domain_connections_setup(void)
{
}

void nrf_802154_platform_timestamper_cross_domain_connections_clear(void)
{
}

void nrf_802154_platform_timestamper_local_domain_connections_setup(uint32_t dppi_ch)
{
    (void)dppi_ch;
}

void nrf_802154_platform_timestamper_local_domain_connections_clear(uint32_t dppi_ch)
{
    (void)dppi_ch;
}

bool nrf_802154_platform_timestamper_captured_timestamp_read(uint64_t * p_captured)
{
    if (p_captured != NULL)
    {
        *p_captured = 0;
    }

    return false;
}
