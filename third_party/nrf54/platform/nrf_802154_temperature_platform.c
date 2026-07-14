/*
 * Copyright (c) 2026, The OpenThread Authors.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * POC temperature stub for the nRF 802.15.4 driver.
 * Returns a fixed 20 C; sufficient for UART/Spinel bring-up.
 */

#include "platform/nrf_802154_temperature.h"

#include <stdint.h>

#define DEFAULT_TEMPERATURE 20

void nrf_802154_temperature_init(void)
{
}

void nrf_802154_temperature_deinit(void)
{
}

int8_t nrf_802154_temperature_get(void)
{
    return DEFAULT_TEMPERATURE;
}
