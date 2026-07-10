/*
 * Copyright (c) 2026, The OpenThread Authors.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * nRF54L GRTC compare -> DPPI -> RADIO task wiring for SL lptimer hardware tasks.
 * Reference: NCS nrf_802154_platform_sl_lptimer_grtc_hw_task.c (NRF54L_SERIES).
 */

#include "nrf_802154_platform_sl_lptimer_grtc_hw_task.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <hal/nrf_ppib.h>
#include <helpers/nrfx_gppi.h>
#include <nrfx_grtc.h>
#include <soc/interconnect/nrfx_gppi_d2ppi.h>

#include "platform/nrf_802154_platform_sl_lptimer.h"

static nrfx_gppi_t        m_gppi_instance;
static bool               m_gppi_initialized;
static nrfx_gppi_handle_t m_peri_rad_handle;
static uint32_t           m_ppib_chan;
static bool               m_cross_domain_connected;

static void platform_gppi_ensure_init(void)
{
    if (m_gppi_initialized)
    {
        return;
    }

    m_gppi_instance.routes    = nrfx_gppi_routes_get();
    m_gppi_instance.route_map = nrfx_gppi_route_map_get();
    m_gppi_instance.nodes     = nrfx_gppi_nodes_get();

    nrfx_gppi_channel_init(NRFX_GPPI_NODE_DPPIC00, NRFX_BIT_MASK(DPPIC00_CH_NUM_SIZE));
    nrfx_gppi_channel_init(NRFX_GPPI_NODE_DPPIC10, NRFX_BIT_MASK(DPPIC10_CH_NUM_SIZE));
    nrfx_gppi_channel_init(NRFX_GPPI_NODE_DPPIC20, NRFX_BIT_MASK(DPPIC20_CH_NUM_SIZE));
    nrfx_gppi_channel_init(NRFX_GPPI_NODE_DPPIC30, NRFX_BIT_MASK(DPPIC30_CH_NUM_SIZE));
    nrfx_gppi_channel_init(NRFX_GPPI_NODE_PPIB00_10, NRFX_BIT_MASK(PPIB00_NTASKSEVENTS_SIZE));
    nrfx_gppi_channel_init(NRFX_GPPI_NODE_PPIB11_21, NRFX_BIT_MASK(PPIB11_NTASKSEVENTS_SIZE));
    nrfx_gppi_channel_init(NRFX_GPPI_NODE_PPIB01_20, NRFX_BIT_MASK(PPIB01_NTASKSEVENTS_SIZE));
    nrfx_gppi_channel_init(NRFX_GPPI_NODE_PPIB22_30, NRFX_BIT_MASK(PPIB22_NTASKSEVENTS_SIZE));

    nrfx_gppi_groups_init(NRFX_GPPI_NODE_DPPIC00, NRFX_BIT_MASK(DPPIC00_GROUP_NUM_SIZE));
    nrfx_gppi_groups_init(NRFX_GPPI_NODE_DPPIC10, NRFX_BIT_MASK(DPPIC10_GROUP_NUM_SIZE));
    nrfx_gppi_groups_init(NRFX_GPPI_NODE_DPPIC20, NRFX_BIT_MASK(DPPIC20_GROUP_NUM_SIZE));
    nrfx_gppi_groups_init(NRFX_GPPI_NODE_DPPIC30, NRFX_BIT_MASK(DPPIC30_GROUP_NUM_SIZE));

    nrfx_gppi_init(&m_gppi_instance);
    m_gppi_initialized = true;
}

void nrf_802154_platform_sl_lptimer_hw_task_cross_domain_connections_setup(uint32_t cc_channel)
{
    nrfx_gppi_resource_t resource = {
        .domain_id = NRFX_GPPI_DOMAIN_RAD,
        .channel   = 0,
    };
    uint32_t eep = nrfx_grtc_event_compare_address_get((uint8_t)cc_channel);
    int      err;

    platform_gppi_ensure_init();

    err = nrfx_gppi_ext_conn_alloc(NRFX_GPPI_DOMAIN_PERI, NRFX_GPPI_DOMAIN_RAD, &m_peri_rad_handle, &resource);
    assert(err == 0);

    err = nrfx_gppi_ep_attach(eep, m_peri_rad_handle);
    assert(err == 0);

    m_ppib_chan = (uint32_t)nrfx_gppi_domain_channel_get(m_peri_rad_handle, NRFX_GPPI_NODE_PPIB11_21);
    assert((int32_t)m_ppib_chan >= 0);

    nrf_ppib_publish_clear(NRF_PPIB11, nrf_ppib_receive_event_get(m_ppib_chan));

    nrfx_gppi_conn_enable(m_peri_rad_handle);
    m_cross_domain_connected = true;
}

void nrf_802154_platform_sl_lptimer_hw_task_cross_domain_connections_clear(void)
{
    if (!m_cross_domain_connected)
    {
        return;
    }

    nrfx_gppi_conn_disable(m_peri_rad_handle);
    nrfx_gppi_domain_conn_free(m_peri_rad_handle);
    m_cross_domain_connected = false;
}

void nrf_802154_platform_sl_lptimer_hw_task_local_domain_connections_setup(uint32_t dppi_ch,
                                                                           uint32_t cc_channel)
{
    (void)cc_channel;

    if (dppi_ch == NRF_802154_SL_HW_TASK_PPI_INVALID)
    {
        return;
    }

    nrf_ppib_publish_set(NRF_PPIB11, nrf_ppib_receive_event_get(m_ppib_chan), dppi_ch);
}

void nrf_802154_platform_sl_lptimer_hw_task_local_domain_connections_clear(void)
{
    nrf_ppib_publish_clear(NRF_PPIB11, nrf_ppib_receive_event_get(m_ppib_chan));
}
