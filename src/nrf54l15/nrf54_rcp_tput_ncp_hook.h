/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Weak hooks from OpenThread NCP into RCP throughput stats (v6 uart_prep split).
 *  Strong implementations live in nrf54_rcp_tput_stats.c when NRF54_RCP_TPUT_STATS=1.
 */

#ifndef NRF54_RCP_TPUT_NCP_HOOK_H_
#define NRF54_RCP_TPUT_NCP_HOOK_H_

#if defined(__GNUC__)
#define NRF54_RCP_TPUT_NCP_WEAK __attribute__((weak))
#else
#define NRF54_RCP_TPUT_NCP_WEAK
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Called after LinkRawTransmitDone() finishes mEncoder.EndFrame() (notify Spinel encode). */
NRF54_RCP_TPUT_NCP_WEAK void nrf54_rcp_tput_ncp_note_link_raw_end_frame(void);

/** Called at entry to NcpHdlc::EncodeAndSend() (tasklet / HDLC encode). */
NRF54_RCP_TPUT_NCP_WEAK void nrf54_rcp_tput_ncp_note_hdlc_encode_and_send_enter(void);

#ifdef __cplusplus
}
#endif

#endif // NRF54_RCP_TPUT_NCP_HOOK_H_
