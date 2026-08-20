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
    /* CC8 hw_task — arm/disarm (CSL receive_at delayed trigger). */
    uint32_t hw_task_prepare_enter;
    uint32_t hw_task_prepare_no_resources;
    uint32_t hw_task_prepare_ok;
    uint32_t hw_task_prepare_fail;
    uint32_t hw_task_cleanup_enter;
    uint32_t hw_task_cleanup_ok;
    uint32_t hw_task_cleanup_wrong_state;
    uint32_t hw_task_update_ppi_enter;
    uint32_t hw_task_update_ppi_ok;
    uint32_t hw_task_update_ppi_wrong_state;
    uint32_t hw_task_update_ppi_too_late;
    uint32_t hw_task_abort_enter;
    uint32_t hw_task_abort_from_prepare;
    uint32_t hw_task_abort_from_cleanup;
    uint32_t hw_task_abort_from_deinit;
    uint32_t cc2_handler_skip_disabled;
    uint32_t lptimer_schedule_at_enter;
    uint32_t lptimer_disable_enter;
    uint32_t lptimer_disable_while_hw_ready;
    uint32_t hw_task_local_setup_enter;
    uint32_t hw_task_local_setup_skip_invalid;
    uint32_t hw_task_local_clear_enter;
    uint32_t last_hw_task_state;
    uint32_t last_hw_task_state_at_no_resources;
    uint32_t last_hw_task_state_at_cleanup_fail;
    uint32_t last_hw_task_ppi;
    uint32_t last_hw_task_fire_lpticks;
    uint32_t last_hw_task_grtc_at_prepare;
    uint32_t last_hw_task_grtc_at_cleanup;
    uint32_t last_hw_task_cc_evt_at_cleanup;
    uint32_t rsch_dly_start;
    uint32_t rsch_dly_start_no_hfclk;
    uint32_t rsch_all_prec_update;
    uint32_t rsch_notify_core;
    uint32_t rsch_pending_set;
    uint32_t rsch_process_pending;
    uint32_t rsch_process_pending_done;
    uint32_t rsch_timeslot_request_false;
    uint32_t rsch_notify_idle_while_requested;
    uint32_t rsch_approved_hw_mismatch;

    /* nrf_802154_core_transmit() deny path (sync TIMESLOT_DENIED 0x07). */
    uint32_t tx_core_deny_cs_enter_fail;
    uint32_t tx_core_deny_terminate_fail;
    uint32_t tx_core_deny_tx_setup_fail;
    uint32_t tx_core_deny_tx_init_immediate;

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
    uint32_t rx_timestamp_ok;
    uint32_t rx_no_timestamp;

    /* otPlatRadioReceiveAt (CSL window scheduling). */
    uint32_t csl_receive_at_enter;
    uint32_t csl_receive_at_ok;
    uint32_t csl_receive_at_fail;
    uint32_t last_csl_channel;
    uint32_t last_csl_win_start;
    uint32_t last_csl_win_duration;
    uint32_t last_csl_receive_at_arg_start;
    uint32_t last_grtc_at_csl_receive_at;

    /* nrf_802154_core rx_init() — CSL/DRX HW trigger path (NRF54_DEBUG_STATS). */
    uint32_t rx_init_enter;
    uint32_t rx_init_hw_enter;
    uint32_t rx_init_skip_no_timeslot;
    uint32_t rx_init_hw_skip_no_timeslot;
    uint32_t rx_init_skip_precond;
    uint32_t rx_init_hw_skip_precond;
    uint32_t rx_init_hw_reach_ppi;
    uint32_t rx_init_hw_ppi_update_fail;
    uint32_t rx_init_hw_ppi_update_ok;
    uint32_t core_receive_delayed_trx_enter;
    uint32_t core_receive_delayed_trx_term_fail;
    uint32_t core_receive_delayed_trx_rx_init;
    uint32_t core_receive_delayed_trx_hw_fallback;
    uint32_t core_receive_delayed_trx_core_enter;
    uint32_t core_receive_delayed_trx_skipped_already_rx;
    uint32_t core_receive_delayed_trx_skipped_tx_ack;

    /* nrf_802154_delayed_trx DRX started_callback / receive_attempt. */
    uint32_t drx_started_callback_enter;
    uint32_t drx_started_callback_canceled;
    uint32_t drx_receive_attempt_enter;
    uint32_t drx_receive_attempt_ok;

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
