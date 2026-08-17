/*
 * nRF54L bare-metal timestamper platform (RAD -> PERI -> GRTC capture).
 * Reference: nrf_802154_platform_timestamper_vZEPHYR.c (NRF54L_SERIES).
 */

#include "platform/nrf_802154_platform_timestamper.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/nrf_dppi.h>
#include <hal/nrf_grtc.h>
#include <hal/nrf_ppib.h>
#include <haly/nrfy_grtc.h>
#include <helpers/nrfx_gppi.h>
#include <nrfx_grtc.h>
#include <soc/interconnect/nrfx_gppi_d2ppi.h>

#include "platform_gppi_nrf54l.h"

static uint8_t            m_timestamp_cc_channel;
static nrfx_gppi_handle_t m_rad_peri_handle;
static uint32_t           m_ppib_chan;
static bool               m_cross_domain_connected;

static void grtc_capture_prepare(uint8_t cc_channel)
{
    nrf_grtc_sys_counter_compare_event_enable(NRF_GRTC, cc_channel);
}


/**
 * API implementation
 */

void nrf_802154_platform_timestamper_init(void)
{
    int err = nrfx_grtc_channel_alloc(&m_timestamp_cc_channel);

    assert(err == 0);
}

void nrf_802154_platform_timestamper_cross_domain_connections_setup(void)
{
    nrfx_gppi_resource_t resource = {
        .domain_id = NRFX_GPPI_DOMAIN_RAD,
        .channel   = 0,
    };
    nrf_grtc_task_t capture_task =
        nrfy_grtc_sys_counter_capture_task_get(m_timestamp_cc_channel);
    uint32_t tep = nrfy_grtc_task_address_get(NRF_GRTC, capture_task);
    int      err;

    if (m_cross_domain_connected)
    {
        return;
    }

    platform_gppi_nrf54l_ensure_init();

    err = nrfx_gppi_ext_conn_alloc(NRFX_GPPI_DOMAIN_RAD, NRFX_GPPI_DOMAIN_PERI, &m_rad_peri_handle,
                                   &resource);
    assert(err == 0);

    err = nrfx_gppi_ep_attach(tep, m_rad_peri_handle);
    assert(err == 0);

    m_ppib_chan = (uint32_t)nrfx_gppi_domain_channel_get(m_rad_peri_handle, NRFX_GPPI_NODE_PPIB11_21);
    assert((int32_t)m_ppib_chan >= 0);

    nrf_ppib_subscribe_clear(NRF_PPIB11, nrf_ppib_send_task_get(m_ppib_chan));

    nrfx_gppi_conn_enable(m_rad_peri_handle);
    m_cross_domain_connected = true;
}

void nrf_802154_platform_timestamper_cross_domain_connections_clear(void)
{
    nrf_grtc_task_t capture_task =
        nrfy_grtc_sys_counter_capture_task_get(m_timestamp_cc_channel);

    NRF_DPPI_ENDPOINT_CLEAR(nrfy_grtc_task_address_get(NRF_GRTC, capture_task));
}

void nrf_802154_platform_timestamper_local_domain_connections_setup(uint32_t dppi_ch)
{
    grtc_capture_prepare(m_timestamp_cc_channel);
    nrf_ppib_subscribe_set(NRF_PPIB11, nrf_ppib_send_task_get(m_ppib_chan), dppi_ch);
}

void nrf_802154_platform_timestamper_local_domain_connections_clear(uint32_t dppi_ch)
{
    (void)dppi_ch;
}

bool nrf_802154_platform_timestamper_captured_timestamp_read(uint64_t * p_captured)
{
    if (p_captured == NULL)
    {
        return false;
    }

    if (nrf_grtc_sys_counter_cc_enable_check(NRF_GRTC, m_timestamp_cc_channel))
    {
        return false;
    }

    *p_captured = nrfy_grtc_sys_counter_cc_get(NRF_GRTC, m_timestamp_cc_channel);
    return true;
}
