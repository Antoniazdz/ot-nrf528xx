/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Weak hooks from nrf_802154 driver / SL lptimer into RCP throughput stats.
 *  Strong implementations live in nrf54_rcp_tput_stats.c when NRF54_RCP_TPUT_STATS=1.
 */

#ifndef NRF_802154_PLATFORM_RCP_TPUT_STATS_HOOK_H_
#define NRF_802154_PLATFORM_RCP_TPUT_STATS_HOOK_H_

#include <stdint.h>

#if defined(__GNUC__)
#define NRF_802154_RCP_TPUT_WEAK __attribute__((weak))
#else
#define NRF_802154_RCP_TPUT_WEAK
#endif

NRF_802154_RCP_TPUT_WEAK void nrf_802154_platform_rcp_tput_stats_tx_frame_started(void);
NRF_802154_RCP_TPUT_WEAK void nrf_802154_platform_rcp_tput_stats_lptimer_fire(uint32_t aSlipTicks);
NRF_802154_RCP_TPUT_WEAK void nrf_802154_platform_rcp_tput_stats_csma_cca_busy(void);
NRF_802154_RCP_TPUT_WEAK void nrf_802154_platform_rcp_tput_stats_csma_backoff_scheduled(void);
NRF_802154_RCP_TPUT_WEAK void nrf_802154_platform_rcp_tput_stats_csma_on_air(uint8_t aBackoffAttempts);

#endif // NRF_802154_PLATFORM_RCP_TPUT_STATS_HOOK_H_
