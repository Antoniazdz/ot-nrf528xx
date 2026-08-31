/*
 * Copyright (c) 2026, The OpenThread Authors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * CSL phase-verification stats (SubMac + platform + driver IE writer).
 */

#include "nrf54_csl_debug.h"

#ifdef NRF54_DEBUG_STATS

#include <stddef.h>

#include <nrfx_grtc.h>

#include "nrf54_debug_stats.h"

#define CSL_US_PER_TEN_SYMBOLS 160U
#define CSL_SAFE_DELTA_US      1000U
#define CSL_DRIVER_MARGIN_US   190U /* RX_SETUP(150) + RX_RAMP(~40) on nRF54L */

static uint32_t sCslPeerShort;
static uint32_t sLastCslChannel;
static uint32_t sLastPlatWinStart;
static uint32_t sLastPlatWinDuration;
static uint32_t sLastPlatPeriodUs;

uint32_t nrf54CslDebugGrtcNowUs(void)
{
    if (!nrfx_grtc_init_check() || !nrfx_grtc_ready_check())
    {
        return 0U;
    }

    return (uint32_t)nrfx_grtc_syscounter_get();
}

static uint32_t periodUsFromTenSymbols(uint16_t aPeriodTenSymbols)
{
    return (uint32_t)aPeriodTenSymbols * CSL_US_PER_TEN_SYMBOLS;
}

static uint32_t cslPhaseUs(uint32_t aTimeUs, uint32_t aPeriodUs)
{
    return (aPeriodUs > 0U) ? (aTimeUs % aPeriodUs) : 0U;
}

static uint32_t cslCircularGapUs(uint32_t aPhaseA, uint32_t aPhaseB, uint32_t aPeriodUs)
{
    uint32_t diff;

    if (aPeriodUs == 0U)
    {
        return 0U;
    }

    diff = (aPhaseA >= aPhaseB) ? (aPhaseA - aPhaseB) : (aPhaseB - aPhaseA);

    if (diff > (aPeriodUs / 2U))
    {
        diff = aPeriodUs - diff;
    }

    return diff;
}

static void recordPredictedGrid(uint32_t aSampleTime, uint32_t aWinStart, uint32_t aWinDuration,
                                uint32_t aNextSample, uint32_t aPeriodUs)
{
    g_nrf54_debug_stats.last_csl_pred_sample_grtc     = aSampleTime;
    g_nrf54_debug_stats.last_csl_pred_win_start_grtc  = aWinStart;
    g_nrf54_debug_stats.last_csl_pred_win_end_grtc    = aWinStart + aWinDuration;
    g_nrf54_debug_stats.last_csl_pred_next_sample_grtc = aNextSample;
    g_nrf54_debug_stats.last_csl_pred_cc8_fire_grtc =
        (aWinStart > (CSL_SAFE_DELTA_US + CSL_DRIVER_MARGIN_US))
            ? (aWinStart - CSL_SAFE_DELTA_US - CSL_DRIVER_MARGIN_US)
            : 0U;
    g_nrf54_debug_stats.last_csl_pred_rx_active_grtc = aWinStart;

    if (aPeriodUs > 0U)
    {
        g_nrf54_debug_stats.last_csl_ot_sample_phase_before_us = cslPhaseUs(aSampleTime, aPeriodUs);
        g_nrf54_debug_stats.last_csl_ot_sample_phase_after_us  = cslPhaseUs(aNextSample, aPeriodUs);
    }
}

static bool rxInWindow(uint32_t aRxTimeUs, uint32_t aWinStart, uint32_t aWinDuration)
{
    uint32_t winEnd;
    int32_t  deltaStart;
    int32_t  deltaEnd;

    if (aWinDuration == 0U)
    {
        return false;
    }

    winEnd     = aWinStart + aWinDuration;
    deltaStart = (int32_t)(aRxTimeUs - aWinStart);
    deltaEnd   = (int32_t)(aRxTimeUs - winEnd);

    return (deltaStart >= 0) && (deltaEnd <= 0);
}

static bool ieee154GetSrcShort(const uint8_t *aPsdu, uint8_t aLength, uint16_t *aSrcShort)
{
    uint16_t fc;
    uint8_t  destMode;
    uint8_t  srcMode;
    uint8_t  idx;

    if (aPsdu == NULL || aLength < 3U || aSrcShort == NULL)
    {
        return false;
    }

    fc       = (uint16_t)aPsdu[0] | ((uint16_t)aPsdu[1] << 8U);
    destMode = (fc >> 10) & 3U;
    srcMode  = (fc >> 14) & 3U;

    if (srcMode != 2U)
    {
        return false;
    }

    idx = 3U;

    if (destMode == 2U)
    {
        if ((uint32_t)idx + 4U > aLength)
        {
            return false;
        }
        idx += 4U;
    }
    else if (destMode == 3U)
    {
        if ((uint32_t)idx + 10U > aLength)
        {
            return false;
        }
        idx += 10U;
    }
    else if (destMode == 1U)
    {
        if ((uint32_t)idx + 2U > aLength)
        {
            return false;
        }
        idx += 2U;
    }

    if ((uint32_t)idx + 4U > aLength)
    {
        return false;
    }

    idx += 2U;
    *aSrcShort = (uint16_t)aPsdu[idx] | ((uint16_t)aPsdu[idx + 1U] << 8U);

    return true;
}

void nrf54CslDebugHandleCslTimer(void)
{
    g_nrf54_debug_stats.csl_handle_csl_timer_enter++;
    g_nrf54_debug_stats.last_grtc_at_handle_csl_timer = nrf54CslDebugGrtcNowUs();
}

void nrf54CslDebugHandleCslReceiveAtEnter(uint8_t aSubMacState, uint32_t aSampleTimeBefore, uint32_t aWinStart,
                                          uint32_t aWinDuration, uint32_t aTimeAhead, uint32_t aTimeAfter,
                                          uint32_t aNextTimerFire, uint32_t aLastSync, uint32_t aSampleTimeAfter)
{
    uint32_t periodUs = sLastPlatPeriodUs;
    uint32_t grtc     = nrf54CslDebugGrtcNowUs();

    g_nrf54_debug_stats.csl_handle_csl_receive_at_enter++;
    g_nrf54_debug_stats.last_grtc_at_handle_csl_receive_at = grtc;
    g_nrf54_debug_stats.last_csl_submac_state              = aSubMacState;

    if (periodUs == 0U && g_nrf54_debug_stats.last_csl_period_us > 0U)
    {
        periodUs = g_nrf54_debug_stats.last_csl_period_us;
    }

    g_nrf54_debug_stats.last_csl_ot_sample_time_before = aSampleTimeBefore;
    g_nrf54_debug_stats.last_csl_ot_sample_time_after  = aSampleTimeAfter;
    g_nrf54_debug_stats.last_csl_ot_win_start          = aWinStart;
    g_nrf54_debug_stats.last_csl_ot_win_end            = aWinStart + aWinDuration;
    g_nrf54_debug_stats.last_csl_ot_win_duration       = aWinDuration;
    g_nrf54_debug_stats.last_csl_ot_time_ahead         = aTimeAhead;
    g_nrf54_debug_stats.last_csl_ot_time_after         = aTimeAfter;
    g_nrf54_debug_stats.last_csl_ot_next_timer_fire    = aNextTimerFire;
    g_nrf54_debug_stats.last_csl_ot_last_sync          = aLastSync;
    g_nrf54_debug_stats.last_csl_ot_win_start_minus_now_us = aWinStart - grtc;

    if (aLastSync <= aSampleTimeBefore)
    {
        g_nrf54_debug_stats.last_csl_ot_elapsed_since_sync_us = aSampleTimeBefore - aLastSync;
    }

    if (periodUs > 0U)
    {
        g_nrf54_debug_stats.last_csl_ot_win_phase_us = cslPhaseUs(aWinStart, periodUs);
        recordPredictedGrid(aSampleTimeBefore, aWinStart, aWinDuration, aSampleTimeAfter, periodUs);
    }

    if ((int32_t)(aWinStart - grtc) < 0)
    {
        g_nrf54_debug_stats.csl_ot_win_in_past++;
    }
    else if ((uint32_t)(aWinStart - grtc) < 400U)
    {
        g_nrf54_debug_stats.csl_ot_win_lead_short++;
    }
    else
    {
        g_nrf54_debug_stats.csl_ot_win_lead_ok++;
    }
}

void nrf54CslDebugReceiveAtOtCalled(uint8_t aSubMacState)
{
    g_nrf54_debug_stats.csl_receive_at_ot_called++;
    g_nrf54_debug_stats.last_csl_submac_state = aSubMacState;
}

void nrf54CslDebugSkipSubMacDisabled(void)
{
    g_nrf54_debug_stats.csl_skip_submac_disabled++;
}

void nrf54CslDebugSkipSubMacReceive(void)
{
    g_nrf54_debug_stats.csl_skip_submac_receive++;
}

void nrf54CslDebugSetCslParams(uint32_t aSampleTimeRadio, uint16_t aPeriodTenSymbols)
{
    uint32_t periodUs = periodUsFromTenSymbols(aPeriodTenSymbols);
    uint32_t grtc     = nrf54CslDebugGrtcNowUs();

    g_nrf54_debug_stats.csl_set_csl_params_enter++;
    g_nrf54_debug_stats.last_csl_init_sample_time_radio = aSampleTimeRadio;
    g_nrf54_debug_stats.last_csl_period_us              = periodUs;
    sLastPlatPeriodUs                                   = periodUs;

    if (periodUs > 0U)
    {
        g_nrf54_debug_stats.last_csl_init_phase_us = cslPhaseUs(aSampleTimeRadio, periodUs);
    }

    g_nrf54_debug_stats.last_grtc_at_handle_csl_timer = grtc;
}

void nrf54CslDebugSyncFromRx(uint32_t aSyncTimestampUs, bool aSecEnhAck)
{
    if (aSecEnhAck)
    {
        g_nrf54_debug_stats.csl_sync_from_rx_enter++;
        g_nrf54_debug_stats.last_csl_sync_timestamp_us = aSyncTimestampUs;
    }
    else
    {
        g_nrf54_debug_stats.csl_sync_from_rx_no_enh_ack++;
    }
}

void nrf54CslDebugSyncFromRxNoEnhAck(void)
{
    g_nrf54_debug_stats.csl_sync_from_rx_no_enh_ack++;
}

void nrf54CslDebugSyncFromTxAck(void)
{
    g_nrf54_debug_stats.csl_sync_from_tx_ack++;
}

void nrf54CslDebugRestartAfterSync(uint32_t aRewoundSampleTime)
{
    g_nrf54_debug_stats.csl_restart_after_sync_enter++;
    g_nrf54_debug_stats.last_grtc_at_restart_sync       = nrf54CslDebugGrtcNowUs();
    g_nrf54_debug_stats.last_csl_sync_rewind_sample_time = aRewoundSampleTime;
}

void nrf54CslDebugUpdateCslSampleTime(uint32_t aAnchorUnwrappedLo, uint16_t aPeriodTenSymbols)
{
    uint32_t periodUs = periodUsFromTenSymbols(aPeriodTenSymbols);

    g_nrf54_debug_stats.update_csl_sample_time_enter++;
    g_nrf54_debug_stats.last_update_csl_sample_time      = aAnchorUnwrappedLo;
    g_nrf54_debug_stats.last_grtc_at_update_csl_sample_time = nrf54CslDebugGrtcNowUs();
    g_nrf54_debug_stats.last_csl_anchor_unwrapped_lo       = aAnchorUnwrappedLo;

    if (periodUs == 0U && g_nrf54_debug_stats.last_csl_period_us > 0U)
    {
        periodUs = g_nrf54_debug_stats.last_csl_period_us;
    }

    if (periodUs > 0U)
    {
        sLastPlatPeriodUs                         = periodUs;
        g_nrf54_debug_stats.last_csl_period_us    = periodUs;
        g_nrf54_debug_stats.last_csl_anchor_phase_us = cslPhaseUs(aAnchorUnwrappedLo, periodUs);
    }
}

void nrf54CslDebugPlatReceiveAt(uint8_t aChannel, uint32_t aWinStart, uint32_t aWinDuration,
                                uint16_t aPeriodTenSymbols)
{
    uint32_t periodUs = periodUsFromTenSymbols(aPeriodTenSymbols);

    if (periodUs > 0U)
    {
        sLastPlatPeriodUs                      = periodUs;
        g_nrf54_debug_stats.last_csl_period_us = periodUs;
    }

    sLastCslChannel                        = aChannel;
    sLastPlatWinStart                      = aWinStart;
    sLastPlatWinDuration                   = aWinDuration;
    g_nrf54_debug_stats.last_csl_win_end_us = aWinStart + aWinDuration;

    if (periodUs > 0U)
    {
        g_nrf54_debug_stats.last_csl_win_phase_us = cslPhaseUs(aWinStart, periodUs);
    }
}

void nrf54CslDebugPlatDrxTimeout(void)
{
    uint32_t periodUs = sLastPlatPeriodUs;

    if (periodUs == 0U)
    {
        periodUs = g_nrf54_debug_stats.last_csl_period_us;
    }

    g_nrf54_debug_stats.last_csl_timeout_win_phase_us = g_nrf54_debug_stats.last_csl_win_phase_us;

    if (periodUs > 0U)
    {
        g_nrf54_debug_stats.last_csl_timeout_phase_gap_us =
            cslCircularGapUs(g_nrf54_debug_stats.last_csl_win_phase_us,
                             g_nrf54_debug_stats.last_parent_rx_phase_us, periodUs);

        if (g_nrf54_debug_stats.last_parent_rx_phase_us != 0U &&
            g_nrf54_debug_stats.last_csl_phase_gap_us > sLastPlatWinDuration)
        {
            g_nrf54_debug_stats.csl_drx_timeout_likely_phase++;
        }
        else if (g_nrf54_debug_stats.csl_rx_from_parent_in_window == 0U &&
                 g_nrf54_debug_stats.csl_rx_from_parent_total == 0U)
        {
            g_nrf54_debug_stats.csl_drx_timeout_likely_phase++;
        }
    }

    nrf54CslDebugWinCapClose();
}

void nrf54CslDebugSetCslPeerShort(nrf54CslPeerShort_t aPeerShort)
{
    sCslPeerShort                           = aPeerShort;
    g_nrf54_debug_stats.last_csl_peer_short = aPeerShort;
}

void nrf54CslDebugParentRxFromPsdu(const uint8_t *aPsdu, uint8_t aLength, uint8_t aChannel, uint32_t aRxTimestampUs,
                                   bool aSecEnhAck)
{
    uint16_t srcShort = 0;
    uint32_t periodUs;
    uint32_t rxPhase;
    uint32_t gap;

    if (sCslPeerShort == 0U)
    {
        return;
    }

    if (!ieee154GetSrcShort(aPsdu, aLength, &srcShort) || srcShort != (uint16_t)sCslPeerShort)
    {
        return;
    }

    g_nrf54_debug_stats.csl_rx_from_parent_total++;

    periodUs = sLastPlatPeriodUs;
    if (periodUs == 0U)
    {
        periodUs = g_nrf54_debug_stats.last_csl_period_us;
    }

    if (periodUs > 0U)
    {
        rxPhase = cslPhaseUs(aRxTimestampUs, periodUs);
        gap     = cslCircularGapUs(rxPhase, g_nrf54_debug_stats.last_csl_win_phase_us, periodUs);

        g_nrf54_debug_stats.last_parent_rx_phase_us = rxPhase;
        g_nrf54_debug_stats.last_csl_phase_gap_us   = gap;
    }

    if (aChannel == (uint8_t)sLastCslChannel)
    {
        g_nrf54_debug_stats.csl_rx_from_parent_csl_ch++;
    }

    if (rxInWindow(aRxTimestampUs, sLastPlatWinStart, sLastPlatWinDuration))
    {
        g_nrf54_debug_stats.csl_rx_from_parent_in_window++;
    }
    else
    {
        g_nrf54_debug_stats.csl_rx_from_parent_outside_window++;
    }

    if (aSecEnhAck)
    {
        g_nrf54_debug_stats.csl_sync_from_rx_enter++;
        g_nrf54_debug_stats.last_csl_sync_timestamp_us = aRxTimestampUs;
    }
    else
    {
        g_nrf54_debug_stats.csl_sync_from_rx_no_enh_ack++;
    }
}

void nrf54CslDebugIeAnchorSet(uint64_t aAnchorTime, uint16_t aPeriodTenSymbols)
{
    uint32_t periodUs = periodUsFromTenSymbols(aPeriodTenSymbols);

    g_nrf54_debug_stats.csl_ie_anchor_set_enter++;
    g_nrf54_debug_stats.last_grtc_at_csl_ie_anchor_set = nrf54CslDebugGrtcNowUs();
    g_nrf54_debug_stats.last_csl_ie_anchor_lo          = (uint32_t)aAnchorTime;

    if (periodUs > 0U)
    {
        sLastPlatPeriodUs                      = periodUs;
        g_nrf54_debug_stats.last_csl_period_us = periodUs;
        g_nrf54_debug_stats.last_csl_ie_pred_next_sample_grtc = (uint32_t)aAnchorTime;
        g_nrf54_debug_stats.last_csl_ie_pred_curr_sample_grtc =
            (uint32_t)(aAnchorTime - (uint64_t)periodUs);
    }
}

void nrf54CslDebugIePeriodSet(uint16_t aPeriodTenSymbols)
{
    uint32_t periodUs = periodUsFromTenSymbols(aPeriodTenSymbols);

    g_nrf54_debug_stats.csl_ie_period_set_enter++;

    if (periodUs > 0U)
    {
        sLastPlatPeriodUs                      = periodUs;
        g_nrf54_debug_stats.last_csl_period_us = periodUs;
    }
}

void nrf54CslDebugIePhaseCalc(uint64_t aAnchorTime, uint64_t aSlTimerUs, uint64_t aRefTimeUs, uint32_t aUsToNext,
                              uint32_t aPhaseTenSymbols, uint16_t aPeriodTenSymbols, bool aAnchorSet, bool aOk,
                              bool aFallback)
{
    uint32_t periodUs = periodUsFromTenSymbols(aPeriodTenSymbols);
    uint32_t grtc     = nrf54CslDebugGrtcNowUs();

    g_nrf54_debug_stats.csl_ie_phase_calc_enter++;

    if (aFallback)
    {
        g_nrf54_debug_stats.csl_ie_phase_calc_fallback++;
    }

    if (aOk)
    {
        g_nrf54_debug_stats.csl_ie_phase_calc_ok++;
    }
    else
    {
        g_nrf54_debug_stats.csl_ie_phase_calc_fail++;
    }

    g_nrf54_debug_stats.last_grtc_at_csl_ie_phase_calc = grtc;
    g_nrf54_debug_stats.last_csl_ie_anchor_lo          = (uint32_t)aAnchorTime;
    g_nrf54_debug_stats.last_csl_ie_sl_timer_us        = (uint32_t)aSlTimerUs;
    g_nrf54_debug_stats.last_csl_ie_ref_time_us        = (uint32_t)aRefTimeUs;
    g_nrf54_debug_stats.last_csl_ie_us_to_next_window  = aUsToNext;
    g_nrf54_debug_stats.last_csl_ie_phase_ten_symbols  = aPhaseTenSymbols;
    g_nrf54_debug_stats.last_csl_ie_period_at_calc     = aPeriodTenSymbols;
    g_nrf54_debug_stats.last_csl_ie_anchor_set_flag    = aAnchorSet ? 1U : 0U;
    g_nrf54_debug_stats.last_csl_ie_sl_minus_grtc_us   = (uint32_t)(int32_t)((uint32_t)aSlTimerUs - grtc);
    g_nrf54_debug_stats.last_csl_ie_pred_mhr_at_grtc = (uint32_t)aRefTimeUs;

    if (periodUs == 0U && g_nrf54_debug_stats.last_csl_period_us > 0U)
    {
        periodUs = g_nrf54_debug_stats.last_csl_period_us;
    }

    if (periodUs > 0U)
    {
        g_nrf54_debug_stats.last_csl_ie_pred_next_sample_grtc = (uint32_t)aAnchorTime;
        g_nrf54_debug_stats.last_csl_ie_pred_curr_sample_grtc =
            (uint32_t)(aAnchorTime - (uint64_t)periodUs);
    }

    if (aOk && !aFallback && g_nrf54_debug_stats.csl_ie_cap_count < 2U)
    {
        uint8_t  idx         = (uint8_t)g_nrf54_debug_stats.csl_ie_cap_count;
        uint32_t mhrPlusPhase = (uint32_t)aRefTimeUs + (aPhaseTenSymbols * CSL_US_PER_TEN_SYMBOLS);
        int32_t  slMinusGrtc = (int32_t)((uint32_t)aSlTimerUs - grtc);

        if (idx == 0U)
        {
            g_nrf54_debug_stats.csl_ie_cap0_ref_mhr_grtc      = (uint32_t)aRefTimeUs;
            g_nrf54_debug_stats.csl_ie_cap0_sl_timer_grtc     = (uint32_t)aSlTimerUs;
            g_nrf54_debug_stats.csl_ie_cap0_grtc_at_hook      = grtc;
            g_nrf54_debug_stats.csl_ie_cap0_sl_minus_grtc_us = (uint32_t)slMinusGrtc;
            g_nrf54_debug_stats.csl_ie_cap0_phase_ten_symbols = aPhaseTenSymbols;
            g_nrf54_debug_stats.csl_ie_cap0_anchor_lo         = (uint32_t)aAnchorTime;
            g_nrf54_debug_stats.csl_ie_cap0_us_to_next        = aUsToNext;
            g_nrf54_debug_stats.csl_ie_cap0_period_at_calc    = aPeriodTenSymbols;
            g_nrf54_debug_stats.csl_ie_cap0_pred_sample_grtc  = (uint32_t)aAnchorTime;
            g_nrf54_debug_stats.csl_ie_cap0_mhr_plus_phase_us = mhrPlusPhase;
        }
        else if (idx == 1U)
        {
            g_nrf54_debug_stats.csl_ie_cap1_ref_mhr_grtc      = (uint32_t)aRefTimeUs;
            g_nrf54_debug_stats.csl_ie_cap1_sl_timer_grtc     = (uint32_t)aSlTimerUs;
            g_nrf54_debug_stats.csl_ie_cap1_grtc_at_hook      = grtc;
            g_nrf54_debug_stats.csl_ie_cap1_sl_minus_grtc_us = (uint32_t)slMinusGrtc;
            g_nrf54_debug_stats.csl_ie_cap1_phase_ten_symbols = aPhaseTenSymbols;
            g_nrf54_debug_stats.csl_ie_cap1_anchor_lo         = (uint32_t)aAnchorTime;
            g_nrf54_debug_stats.csl_ie_cap1_us_to_next        = aUsToNext;
            g_nrf54_debug_stats.csl_ie_cap1_period_at_calc    = aPeriodTenSymbols;
            g_nrf54_debug_stats.csl_ie_cap1_pred_sample_grtc  = (uint32_t)aAnchorTime;
            g_nrf54_debug_stats.csl_ie_cap1_mhr_plus_phase_us = mhrPlusPhase;
        }

        g_nrf54_debug_stats.csl_ie_cap_count = idx + 1U;
    }
}

static void winCapStorePlan(uint8_t aIdx, uint32_t aGrtcAtPlatPlan)
{
    volatile nrf54_debug_stats_t *st = &g_nrf54_debug_stats;

    if (aIdx == 0U)
    {
        st->csl_cap0_grtc_at_ot_plan       = st->last_grtc_at_handle_csl_receive_at;
        st->csl_cap0_grtc_at_plat_plan     = aGrtcAtPlatPlan;
        st->csl_cap0_ot_sample_before      = st->last_csl_ot_sample_time_before;
        st->csl_cap0_ot_sample_after       = st->last_csl_ot_sample_time_after;
        st->csl_cap0_ot_win_start          = st->last_csl_ot_win_start;
        st->csl_cap0_ot_win_end            = st->last_csl_ot_win_end;
        st->csl_cap0_ot_win_duration       = st->last_csl_ot_win_duration;
        st->csl_cap0_anchor_phase_us       = st->last_csl_anchor_phase_us;
        st->csl_cap0_win_phase_us          = st->last_csl_win_phase_us;
        st->csl_cap0_ie_phase_ten_symbols  = st->last_csl_ie_phase_ten_symbols;
        st->csl_cap0_ie_anchor_lo          = st->last_csl_ie_anchor_lo;
        st->csl_cap0_ie_ref_mhr_grtc       = st->last_csl_ie_ref_time_us;
        st->csl_cap0_ie_sl_timer_grtc      = st->last_csl_ie_sl_timer_us;
        st->csl_cap0_grtc_at_drx_sched     = st->last_grtc_at_drx_receive;
        st->csl_cap0_drx_rx_time_lo        = st->last_drx_rx_time_arg_lo;
        st->csl_cap0_drx_trigger_lo        = st->last_drx_trigger_time_lo;
        st->csl_cap0_pred_cc8_fire_grtc    = st->last_csl_pred_cc8_fire_grtc;
        st->csl_grtc_probe0_sample_grtc      = st->last_csl_ot_sample_time_before;
    }
    else if (aIdx == 1U)
    {
        st->csl_cap1_grtc_at_ot_plan       = st->last_grtc_at_handle_csl_receive_at;
        st->csl_cap1_grtc_at_plat_plan     = aGrtcAtPlatPlan;
        st->csl_cap1_ot_sample_before      = st->last_csl_ot_sample_time_before;
        st->csl_cap1_ot_sample_after       = st->last_csl_ot_sample_time_after;
        st->csl_cap1_ot_win_start          = st->last_csl_ot_win_start;
        st->csl_cap1_ot_win_end            = st->last_csl_ot_win_end;
        st->csl_cap1_ot_win_duration       = st->last_csl_ot_win_duration;
        st->csl_cap1_anchor_phase_us       = st->last_csl_anchor_phase_us;
        st->csl_cap1_win_phase_us          = st->last_csl_win_phase_us;
        st->csl_cap1_ie_phase_ten_symbols  = st->last_csl_ie_phase_ten_symbols;
        st->csl_cap1_ie_anchor_lo          = st->last_csl_ie_anchor_lo;
        st->csl_cap1_ie_ref_mhr_grtc       = st->last_csl_ie_ref_time_us;
        st->csl_cap1_ie_sl_timer_grtc      = st->last_csl_ie_sl_timer_us;
        st->csl_cap1_grtc_at_drx_sched     = st->last_grtc_at_drx_receive;
        st->csl_cap1_drx_rx_time_lo        = st->last_drx_rx_time_arg_lo;
        st->csl_cap1_drx_trigger_lo        = st->last_drx_trigger_time_lo;
        st->csl_cap1_pred_cc8_fire_grtc    = st->last_csl_pred_cc8_fire_grtc;
        st->csl_grtc_probe1_sample_grtc      = st->last_csl_ot_sample_time_before;
        st->csl_grtc_probe_sample_delta =
            st->csl_grtc_probe1_sample_grtc - st->csl_grtc_probe0_sample_grtc;
        st->csl_grtc_period_probe_valid = 1U;
    }
}

static void winCapStoreClose(uint8_t aIdx, uint32_t aGrtcAtClose)
{
    if (aIdx == 0U)
    {
        g_nrf54_debug_stats.csl_cap0_grtc_at_close    = aGrtcAtClose;
        g_nrf54_debug_stats.csl_cap0_hw_cc8_fire_grtc = g_nrf54_debug_stats.last_hw_task_grtc_at_update_ppi;
        g_nrf54_debug_stats.csl_grtc_probe0_close_grtc = aGrtcAtClose;
    }
    else if (aIdx == 1U)
    {
        g_nrf54_debug_stats.csl_cap1_grtc_at_close    = aGrtcAtClose;
        g_nrf54_debug_stats.csl_cap1_hw_cc8_fire_grtc = g_nrf54_debug_stats.last_hw_task_grtc_at_update_ppi;
        g_nrf54_debug_stats.csl_grtc_probe1_close_grtc = aGrtcAtClose;
        g_nrf54_debug_stats.csl_grtc_probe_close_delta =
            g_nrf54_debug_stats.csl_grtc_probe1_close_grtc - g_nrf54_debug_stats.csl_grtc_probe0_close_grtc;
    }
}

void nrf54CslDebugWinCapPlan(void)
{
    uint32_t idx = g_nrf54_debug_stats.csl_win_cap_plan_count;

    if (idx >= 2U)
    {
        return;
    }

    winCapStorePlan((uint8_t)idx, nrf54CslDebugGrtcNowUs());
    g_nrf54_debug_stats.csl_win_cap_plan_count = idx + 1U;
}

void nrf54CslDebugWinCapClose(void)
{
    uint32_t idx = g_nrf54_debug_stats.csl_win_cap_close_count;

    if (idx >= 2U)
    {
        return;
    }

    winCapStoreClose((uint8_t)idx, nrf54CslDebugGrtcNowUs());
    g_nrf54_debug_stats.csl_win_cap_close_count = idx + 1U;
}

void nrf54CslDebugDrxReceiveScheduled(uint64_t aRxTimeArg, uint64_t aTriggerTime, uint32_t aTimeoutLength, bool aOk)
{
    (void)aOk;

    g_nrf54_debug_stats.drx_receive_enter++;
    g_nrf54_debug_stats.last_grtc_at_drx_receive   = nrf54CslDebugGrtcNowUs();
    g_nrf54_debug_stats.last_drx_rx_time_arg_lo     = (uint32_t)aRxTimeArg;
    g_nrf54_debug_stats.last_drx_trigger_time_lo    = (uint32_t)aTriggerTime;
    g_nrf54_debug_stats.last_drx_timeout_length_us  = aTimeoutLength;
}

#endif /* NRF54_DEBUG_STATS */
