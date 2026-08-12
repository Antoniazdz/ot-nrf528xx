/*
 * Copyright (c) 2026, The OpenThread Authors.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Clock ready callbacks for the nRF 802.15.4 SL stack.
 *
 * hfclk_ready is implemented in nrf_802154_sl_rsch.c. lfclk_ready is provided
 * here for bare-metal POC; full GRTC lptimer may extend this later.
 */

#include "platform/nrf_802154_clock.h"

void nrf_802154_clock_lfclk_ready(void)
{
    /* intentionally empty */
}
