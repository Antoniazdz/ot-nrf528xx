/*
 * Copyright (c) 2026, The OpenThread Authors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * CSL phase-verification stats (SubMac hooks + platform RX/timeout snapshots).
 */

#include "nrf54_csl_debug.h"

#ifdef NRF54_DEBUG_STATS

#include <stddef.h>

#include "nrf54_debug_stats.h"

#define CSL_US_PER_TEN_SYMBOLS 160U

static uint32_t sCslPeerShort;
static uint32_t sLastCslChannel;
static uint32_t sLastPlatWinStart;
static uint32_t sLastPlatWinDuration;
static uint32_t sLastPlatPeriodUs;

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

    idx += 2U; /* src PAN */
    *aSrcShort = (uint16_t)aPsdu[idx] | ((uint16_t)aPsdu[idx + 1U] << 8U);

    return true;
}

void nrf54CslDebugHandleCslTimer(void)
{
    g_nrf54_debug_stats.csl_handle_csl_timer_enter++;
}

void nrf54CslDebugHandleCslReceiveAtEnter(uint8_t aSubMacState, uint32_t aWinStart, uint32_t aWinDuration)
{
    uint32_t periodUs = sLastPlatPeriodUs;

    g_nrf54_debug_stats.csl_handle_csl_receive_at_enter++;
    g_nrf54_debug_stats.last_csl_submac_state = aSubMacState;

    if (periodUs == 0U && g_nrf54_debug_stats.last_csl_period_us > 0U)
    {
        periodUs = g_nrf54_debug_stats.last_csl_period_us;
    }

    if (periodUs > 0U)
    {
        g_nrf54_debug_stats.last_csl_ot_win_phase_us = cslPhaseUs(aWinStart, periodUs);
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

    g_nrf54_debug_stats.csl_set_csl_params_enter++;
    g_nrf54_debug_stats.last_csl_init_sample_time_radio = aSampleTimeRadio;
    g_nrf54_debug_stats.last_csl_period_us              = periodUs;
    sLastPlatPeriodUs                                   = periodUs;

    if (periodUs > 0U)
    {
        g_nrf54_debug_stats.last_csl_init_phase_us = cslPhaseUs(aSampleTimeRadio, periodUs);
    }
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
            /* No parent RX ever: phase never aligned (init from GetNow). */
            g_nrf54_debug_stats.csl_drx_timeout_likely_phase++;
        }
    }
}

void nrf54CslDebugSetCslPeerShort(nrf54CslPeerShort_t aPeerShort)
{
    sCslPeerShort                          = aPeerShort;
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
        g_nrf54_debug_stats.last_csl_phase_gap_us    = gap;
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

#endif /* NRF54_DEBUG_STATS */
