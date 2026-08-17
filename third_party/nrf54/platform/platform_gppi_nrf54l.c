/*
 * Copyright (c) 2026, The OpenThread Authors.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Shared nrfx_gppi bootstrap for nRF54L cross-domain wiring.
 */

#include "platform_gppi_nrf54l.h"

#include <stdbool.h>

#include <helpers/nrfx_gppi.h>
#include <soc/interconnect/nrfx_gppi_d2ppi.h>

static nrfx_gppi_t m_gppi_instance;
static bool        m_gppi_initialized;

void platform_gppi_nrf54l_ensure_init(void)
{
    if (m_gppi_initialized)
    {
        return;
    }

    m_gppi_instance.routes    = nrfx_gppi_routes_get();
    m_gppi_instance.route_map = nrfx_gppi_route_map_get();
    m_gppi_instance.nodes     = nrfx_gppi_nodes_get();

    /*
     * Match NCS lptimer_grtc_hw_task.c (NRF54L): no blanket DPPIC init.
     * Only seed GPPI pools for nodes on the PERI<->RAD cross-domain routes.
     *
     * DPPIC10 channels 3–23 are owned by the 802.15.4 driver (nrf_dppi_* on
     * NRF_802154_DPPIC_INSTANCE). Claiming the full DPPIC10 mask here caused
     * GPPI/driver conflicts when CC8 cross-domain wiring was enabled.
     *
     * cross_domain setup passes resource.channel = 0 for NRFX_GPPI_DOMAIN_RAD.
     */
    nrfx_gppi_channel_init(NRFX_GPPI_NODE_DPPIC20, NRFX_BIT_MASK(DPPIC20_CH_NUM_SIZE));
    nrfx_gppi_channel_init(NRFX_GPPI_NODE_PPIB11_21, NRFX_BIT_MASK(PPIB11_NTASKSEVENTS_SIZE));
    nrfx_gppi_channel_init(NRFX_GPPI_NODE_DPPIC10, 1UL << 0);

    nrfx_gppi_init(&m_gppi_instance);
    m_gppi_initialized = true;
}
