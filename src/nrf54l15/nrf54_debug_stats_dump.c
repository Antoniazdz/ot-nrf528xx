/*
 * Copyright (c) 2026, The OpenThread Authors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "nrf54_debug_stats_dump.h"

#include <openthread-core-config.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <openthread/platform/logging.h>

#include "nrf54_debug_stats.h"

#define LOG(...) otPlatLog(OT_LOG_LEVEL_INFO, OT_LOG_REGION_PLATFORM, __VA_ARGS__)

static void emit_line(Nrf54StatsEmitLineFn aEmit, void *aContext, const char *aFmt, ...)
{
    char    buf[96];
    va_list ap;

    va_start(ap, aFmt);
    vsnprintf(buf, sizeof(buf), aFmt, ap);
    va_end(ap);

    /* UART/diag path: avoid otPlatLog per line (RTT can block and stall CLI Done). */
    if (aEmit == NULL)
    {
        LOG("%s", buf);
    }
    else
    {
        aEmit(aContext, buf);
    }
}

#define STAT_EMIT(_field) \
    emit_line(aEmit, aContext, #_field "=%lu", (unsigned long)g_nrf54_debug_stats._field)

void nrf54DebugStatsClear(void)
{
    memset((void *)&g_nrf54_debug_stats, 0, sizeof(g_nrf54_debug_stats));
}

void nrf54DebugStatsDumpSummaryEmit(Nrf54StatsEmitLineFn aEmit, void *aContext)
{
    emit_line(aEmit, aContext, "nrf54_debug_stats size=%u addr=%p", (unsigned)sizeof(g_nrf54_debug_stats),
              (void *)&g_nrf54_debug_stats);

    emit_line(aEmit, aContext, "--- CSL ReceiveAt ---");
    STAT_EMIT(csl_receive_at_enter);
    STAT_EMIT(csl_receive_at_ok);
    STAT_EMIT(csl_receive_at_fail);
    STAT_EMIT(csl_plat_win_in_past);
    STAT_EMIT(csl_plat_win_lead_short);
    STAT_EMIT(csl_plat_win_lead_ok);
    STAT_EMIT(last_csl_plat_win_past_by_us);
    STAT_EMIT(last_csl_channel);
    STAT_EMIT(last_csl_win_start);
    STAT_EMIT(last_csl_win_duration);
    STAT_EMIT(last_csl_receive_at_arg_start);
    STAT_EMIT(last_grtc_at_csl_receive_at);
    STAT_EMIT(last_csl_start_minus_now_us);
    STAT_EMIT(last_driver_state_at_csl_receive_at);
    STAT_EMIT(last_rx_on_when_idle_at_csl_receive_at);
    STAT_EMIT(update_csl_sample_time_enter);
    STAT_EMIT(last_update_csl_sample_time);

    emit_line(aEmit, aContext, "--- DRX / delayed TRX ---");
    STAT_EMIT(drx_receive_enter);
    STAT_EMIT(drx_receive_ok);
    STAT_EMIT(drx_receive_fail_duplicate_id);
    STAT_EMIT(drx_receive_fail_no_slot);
    STAT_EMIT(drx_receive_fail_rsch);
    STAT_EMIT(last_drx_fail_reason);
    STAT_EMIT(drx_trigger_in_past);
    STAT_EMIT(drx_trigger_lead_short);
    STAT_EMIT(drx_trigger_lead_ok);
    STAT_EMIT(last_drx_trigger_minus_now_us);
    STAT_EMIT(drx_rsch_request_enter);
    STAT_EMIT(drx_rsch_request_ok);
    STAT_EMIT(drx_rsch_request_fail);
    STAT_EMIT(drx_started_callback_enter);
    STAT_EMIT(drx_started_callback_canceled);
    STAT_EMIT(drx_receive_attempt_enter);
    STAT_EMIT(drx_receive_attempt_ok);
    STAT_EMIT(drx_timeout_notify);
    STAT_EMIT(core_receive_delayed_trx_enter);
    STAT_EMIT(core_receive_delayed_trx_skipped_already_rx);
    STAT_EMIT(core_receive_delayed_trx_skipped_tx_ack);
    STAT_EMIT(core_receive_delayed_trx_rx_init);
    STAT_EMIT(rx_init_hw_enter);
    STAT_EMIT(rx_init_hw_ppi_update_ok);
    STAT_EMIT(rx_init_hw_ppi_update_fail);

    emit_line(aEmit, aContext, "--- Sleep / idle ---");
    STAT_EMIT(csl_drx_timeout_enter);
    STAT_EMIT(csl_drx_timeout_schedule_sleep);
    STAT_EMIT(csl_sleep_after_poll_schedule);
    STAT_EMIT(csl_platform_sleep_suppressed);
    STAT_EMIT(switch_to_idle_enter);
    STAT_EMIT(switch_to_idle_sleep);
    STAT_EMIT(switch_to_idle_rx);
    STAT_EMIT(rx_on_when_idle_set_enter);
    STAT_EMIT(rx_on_when_idle_set_false);

    emit_line(aEmit, aContext, "--- hw_task (CSL fire) ---");
    STAT_EMIT(hw_task_prepare_enter);
    STAT_EMIT(hw_task_prepare_ok);
    STAT_EMIT(hw_task_prepare_fail);
    STAT_EMIT(hw_task_prepare_no_resources);
    STAT_EMIT(hw_task_update_ppi_enter);
    STAT_EMIT(hw_task_update_ppi_ok);
    STAT_EMIT(hw_task_update_ppi_too_late);
    STAT_EMIT(hw_task_update_ppi_cc_already_triggered);
    STAT_EMIT(last_hw_task_grtc_at_update_ppi);
    STAT_EMIT(last_hw_task_cc_at_update_ppi);

    emit_line(aEmit, aContext, "--- TX/RX ---");
    STAT_EMIT(tx_enter);
    STAT_EMIT(tx_done_success);
    STAT_EMIT(tx_done_no_ack);
    STAT_EMIT(tx_done_busy);
    STAT_EMIT(rx_frame);
    STAT_EMIT(csl_alarm_process_early);
    STAT_EMIT(ot_us_alarm_fired);
}

void nrf54DebugStatsDumpSummary(void)
{
    nrf54DebugStatsDumpSummaryEmit(NULL, NULL);
}

void nrf54DebugStatsDumpHandoffEmit(Nrf54StatsEmitLineFn aEmit, void *aContext)
{
    emit_line(aEmit, aContext, "nrf54_handoff size=%u", (unsigned)sizeof(g_nrf54_debug_stats));

    emit_line(aEmit, aContext, "--- platform CSL ---");
    STAT_EMIT(csl_receive_at_enter);
    STAT_EMIT(csl_receive_at_ok);
    STAT_EMIT(csl_receive_at_fail);
    STAT_EMIT(last_csl_start_minus_now_us);
    STAT_EMIT(last_driver_state_at_csl_receive_at);
    STAT_EMIT(last_rx_on_when_idle_at_csl_receive_at);
    STAT_EMIT(csl_platform_sleep_suppressed);
    STAT_EMIT(csl_sleep_force_rx_terminate);
    STAT_EMIT(csl_receive_at_post_schedule_sleep);

    emit_line(aEmit, aContext, "--- window rejected before scheduling ---");
    STAT_EMIT(csl_plat_win_in_past);
    STAT_EMIT(drx_receive_fail_duplicate_id);
    STAT_EMIT(drx_receive_fail_no_slot);
    STAT_EMIT(drx_receive_fail_rsch);
    /* Did the cancel that precedes every re-request drop a window still pending? */
    STAT_EMIT(drx_scheduled_cancel_enter);
    STAT_EMIT(drx_scheduled_cancel_not_found);
    STAT_EMIT(drx_scheduled_cancel_ok);

    emit_line(aEmit, aContext, "--- DRX schedule / fire ---");
    STAT_EMIT(drx_receive_enter);
    STAT_EMIT(drx_receive_ok);
    STAT_EMIT(drx_rsch_request_ok);
    STAT_EMIT(drx_started_callback_enter);
    STAT_EMIT(drx_receive_attempt_enter);
    STAT_EMIT(drx_receive_attempt_ok);
    STAT_EMIT(drx_receive_attempt_fail);
    STAT_EMIT(drx_timeout_notify);

    emit_line(aEmit, aContext, "--- core handoff ---");
    STAT_EMIT(core_receive_delayed_trx_enter);
    STAT_EMIT(core_receive_delayed_trx_skipped_already_rx);
    STAT_EMIT(core_receive_delayed_trx_rx_init);
    STAT_EMIT(rx_init_hw_enter);
    STAT_EMIT(rx_init_hw_ppi_update_ok);
    STAT_EMIT(last_core_state_at_drx_receive_attempt);
    STAT_EMIT(last_core_state_at_delayed_skip_rx);

    emit_line(aEmit, aContext, "--- CSL IE advertised to parent ---");
    STAT_EMIT(csl_ie_phase_calc_anchor_path);
    STAT_EMIT(csl_ie_phase_calc_fallback);
    STAT_EMIT(last_csl_ie_ref_time);
    STAT_EMIT(last_csl_ie_anchor);
    STAT_EMIT(last_csl_ie_phase_us);
    STAT_EMIT(last_update_csl_sample_time);

    emit_line(aEmit, aContext, "--- idle / hw_task ---");
    STAT_EMIT(csl_sleep_after_window);
    STAT_EMIT(hw_task_prepare_ok);
    STAT_EMIT(hw_task_update_ppi_ok);
}

void nrf54DebugStatsDumpRawEmit(Nrf54StatsEmitLineFn aEmit, void *aContext)
{
    const uint32_t *words     = (const uint32_t *)&g_nrf54_debug_stats;
    const size_t    wordCount = sizeof(g_nrf54_debug_stats) / sizeof(uint32_t);

    emit_line(aEmit, aContext, "nrf54_debug_stats raw words=%u", (unsigned)wordCount);

    for (size_t i = 0; i < wordCount; i += 4)
    {
        emit_line(aEmit, aContext, "+%04x: %08lx %08lx %08lx %08lx", (unsigned)(i * sizeof(uint32_t)),
                  (unsigned long)(i + 0 < wordCount ? words[i + 0] : 0),
                  (unsigned long)(i + 1 < wordCount ? words[i + 1] : 0),
                  (unsigned long)(i + 2 < wordCount ? words[i + 2] : 0),
                  (unsigned long)(i + 3 < wordCount ? words[i + 3] : 0));
    }
}

void nrf54DebugStatsDumpRaw(void)
{
    nrf54DebugStatsDumpRawEmit(NULL, NULL);
}
