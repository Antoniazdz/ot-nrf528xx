/*
 * Copyright (c) 2026, The OpenThread Authors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Volatile debug counters for nRF54 RCP bring-up (session d26209).
 */

#ifndef NRF54_DEBUG_STATS_H_
#define NRF54_DEBUG_STATS_H_

#include <stdint.h>

typedef struct
{   uint32_t grtc2_isr_enter;
    uint32_t compare_handler_enter;   /* na samym początku, PRZED m_enabled */
    uint32_t sl_timer_handler_enter;
    uint32_t sl_timer_fire_immediate;
    uint32_t cc2_timer_fires;
    uint32_t cc0_timer_fires;
    uint32_t cc1_timer_fires;
    uint32_t cc3_timer_fires;
    uint32_t hw_task_prepare_ok;
    uint32_t hw_task_prepare_fail;
    uint32_t rsch_dly_start;
    uint32_t rsch_dly_start_no_hfclk;
    uint32_t rsch_all_prec_update;
    uint32_t rsch_notify_core;
    uint32_t rsch_pending_set;
    uint32_t rsch_process_pending;
    uint32_t rsch_process_pending_done;
    uint32_t tx_fail_busy_channel;
    uint32_t hfclk_ready_calls;

    uint32_t tx_enter;
    uint32_t tx_csma_enter;
    uint32_t tx_csma_immediate_error;
    uint32_t tx_raw_enter;
    uint32_t tx_raw_immediate_error;
    uint32_t last_driver_state;

    /* Last immediate TX fail — updated only on fail, not overwritten by later success. */
    uint32_t last_fail_driver_state;
    uint32_t last_fail_immediate_error;
    uint32_t last_fail_tx_length;
    uint32_t last_fail_tx_channel;
    uint32_t last_fail_ack_requested;

    /* Ping-sized unicast (ACK req, len 60–120): Echo Request/Reply vs MLE/broadcast. */
    uint32_t tx_fail_ping_sized;
    uint32_t last_ping_fail_driver_state;
    uint32_t last_ping_fail_immediate_error;
    uint32_t last_ping_fail_tx_length;

    /* Driver state histogram at immediate fail (state before transmit_raw). */
    uint32_t tx_fail_state_sleep;
    uint32_t tx_fail_state_rx;
    uint32_t tx_fail_state_tx_ack;
    uint32_t tx_fail_state_cca_tx;
    uint32_t tx_fail_state_tx;
    uint32_t tx_fail_state_rx_ack;
    uint32_t tx_fail_state_other;

    /* Immediate error code histogram (synchronous transmit_raw / csma return). */
    uint32_t tx_raw_err_timeslot_denied;
    uint32_t tx_raw_err_invalid_request;
    uint32_t tx_raw_err_key_id_invalid;
    uint32_t tx_raw_err_frame_counter_error;
    uint32_t tx_raw_err_timestamp_encoding;
    uint32_t tx_raw_err_other;

    uint32_t tx_driver_busy;
    uint32_t tx_driver_no_ack;
    uint32_t tx_driver_timeslot_ended;
    uint32_t tx_driver_aborted;
    uint32_t tx_driver_timeslot_denied;
    uint32_t tx_driver_success_ack;
    uint32_t tx_driver_success_no_ack;

    uint32_t tx_done_busy;
    uint32_t tx_done_no_ack;
    uint32_t tx_done_success;

    uint32_t rx_frame;

    uint32_t last_tx_length;
    uint32_t last_tx_channel;
    uint32_t last_tx_csma;
    uint32_t last_tx_max_backoffs;
    uint32_t last_tx_immediate_error;
    uint32_t last_driver_error;
    uint32_t last_ack_present;

    /* nRF52 model: counter inject in otPlatRadioTransmit, AES-CCM in nrf_802154_tx_started. */
    uint32_t tx_counter_inject;              /* KeyId + frame counter set before driver TX */
    uint32_t tx_late_encrypt_hook_enter;     /* nrf_802154_tx_started calls */
    uint32_t tx_late_encrypt;                /* otMacFrameProcessTransmitAesCcm executed */
    uint32_t tx_late_encrypt_ping;           /* late encrypt on ping-sized frame */
    uint32_t tx_late_encrypt_skip_processed; /* skipped: mIsSecurityProcessed already set */
    uint32_t tx_late_encrypt_skip_not_secured; /* skipped: not secured KeyIdMode1 */

    uint32_t last_tx_counter_injected;       /* snapshot: last TX injected frame counter */
    uint32_t last_tx_late_encrypted;         /* snapshot: last TX late-encrypted in hook */
} nrf54_debug_stats_t;

extern volatile nrf54_debug_stats_t g_nrf54_debug_stats;

#endif /* NRF54_DEBUG_STATS_H_ */
