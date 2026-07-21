/*
 * Copyright (c) 2026, The OpenThread Authors.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef NRF_802154_PLATFORM_SL_LPTIMER_GRTC_HW_TASK_H_
#define NRF_802154_PLATFORM_SL_LPTIMER_GRTC_HW_TASK_H_

#include <stdint.h>

/**
 * @brief Sets up cross-domain hardware connections necessary to trigger a RADIO task.
 *
 * @param cc_channel  GRTC compare channel used for the hardware task event.
 */
/** @return 0 on success, negative errno from nrfx GPPI on failure. */
int nrf_802154_platform_sl_lptimer_hw_task_cross_domain_connections_setup(uint32_t cc_channel);

/** @brief Clears cross-domain hardware connections set up for hardware tasks. */
void nrf_802154_platform_sl_lptimer_hw_task_cross_domain_connections_clear(void);

/**
 * @brief Sets up local-domain hardware connections necessary to trigger a RADIO task.
 *
 * @param dppi_ch     Radio-domain DPPI channel subscribed by the RADIO task.
 * @param cc_channel  GRTC compare channel (unused on nRF54L; kept for API parity).
 */
void nrf_802154_platform_sl_lptimer_hw_task_local_domain_connections_setup(uint32_t dppi_ch,
                                                                           uint32_t cc_channel);

/** @brief Clears local-domain hardware connections for hardware tasks. */
void nrf_802154_platform_sl_lptimer_hw_task_local_domain_connections_clear(void);

#endif /* NRF_802154_PLATFORM_SL_LPTIMER_GRTC_HW_TASK_H_ */
