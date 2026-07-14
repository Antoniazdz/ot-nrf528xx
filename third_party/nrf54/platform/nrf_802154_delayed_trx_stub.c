/*
 * Copyright (c) 2026, The OpenThread Authors.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * POC stub for delayed RX when NRF_802154_DELAYED_TRX_ENABLED=0 in the driver.
 * radio_nrf54.c still references nrf_802154_receive_at for Thread 1.2+.
 */

#include <stdbool.h>
#include <stdint.h>

bool nrf_802154_receive_at(uint64_t rx_time,
                           uint32_t timeout,
                           uint8_t  channel,
                           uint32_t id)
{
    (void)rx_time;
    (void)timeout;
    (void)channel;
    (void)id;

    return false;
}
