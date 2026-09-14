/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "nrf54_rcp_tput_stats.h"

#if NRF54_RCP_TPUT_STATS

#include "platform-nrf5.h"
#include "nrf54_rcp_tput_ncp_hook.h"
#include "nrf_802154_platform_rcp_tput_stats_hook.h"

#include <stdio.h>
#include <limits.h>

#include <SEGGER_RTT.h>

nrf54_rcp_tput_stats_t g_nrf54_rcp_tput_stats;

static volatile uint64_t sUartEndRxIrqUs;
static uint64_t          sLastUartDeliverUs;
static uint64_t          sRadioTxStartUs;
static uint64_t          sRadioTxFrameStartedUs;
static uint64_t          sRadioTxDoneUs;
static uint64_t          sRadioProcessTxDoneUs;
static uint64_t          sNotifyUartTxSendUs;
static uint64_t          sNotifyUartTxDoneUs;
static uint64_t          sNotifySpinelEndFrameUs;
static uint64_t          sNotifyEncodeAndSendUs;
static uint64_t          sHostCmdEndRxIrqUs;
static bool              sAwaitingHostCmd;
static bool              sBurstInterFrame;

uint64_t nrf54RcpTputStatsNowUs(void)
{
    return nrf5AlarmGetCurrentTime();
}

void nrf54RcpTputStatsRecordMax(uint32_t *aMaxField, uint32_t aValueUs)
{
    if (aMaxField != NULL && *aMaxField < aValueUs)
    {
        *aMaxField = aValueUs;
    }
}

void nrf54RcpTputStatsRecordSumMax(uint32_t *aSumField, uint32_t *aMaxField, uint32_t aValueUs)
{
    if (aSumField != NULL)
    {
        *aSumField += aValueUs;
    }

    nrf54RcpTputStatsRecordMax(aMaxField, aValueUs);
}

static void recordLatencySample(uint32_t *aMaxField, uint32_t *aSumField, uint32_t *aCountField, uint32_t aValueUs)
{
    if (aCountField != NULL)
    {
        (*aCountField)++;
    }

    nrf54RcpTputStatsRecordMax(aMaxField, aValueUs);

    if (aSumField != NULL)
    {
        *aSumField += aValueUs;
    }
}

static void recordLatencySampleMin(uint32_t *aMinField,
                                   uint32_t *aMaxField,
                                   uint32_t *aSumField,
                                   uint32_t *aCountField,
                                   uint32_t aValueUs)
{
    if (aMinField != NULL && *aMinField > aValueUs)
    {
        *aMinField = aValueUs;
    }

    recordLatencySample(aMaxField, aSumField, aCountField, aValueUs);
}

static void recordCsmaHistogram(uint32_t aCsmaUs)
{
    if (aCsmaUs < 320u)
    {
        g_nrf54_rcp_tput_stats.csma_hist_0_320++;
    }
    else if (aCsmaUs < 640u)
    {
        g_nrf54_rcp_tput_stats.csma_hist_320_640++;
    }
    else if (aCsmaUs < 1280u)
    {
        g_nrf54_rcp_tput_stats.csma_hist_640_1280++;
    }
    else if (aCsmaUs < 2000u)
    {
        g_nrf54_rcp_tput_stats.csma_hist_1280_2000++;
    }
    else
    {
        g_nrf54_rcp_tput_stats.csma_hist_2000_plus++;
    }
}

static void recordBurstSample(uint32_t *aMinField,
                              uint32_t *aMaxField,
                              uint32_t *aSumField,
                              uint32_t *aCountField,
                              uint32_t aValueUs)
{
    recordLatencySampleMin(aMinField, aMaxField, aSumField, aCountField, aValueUs);
}

static void recordBurstHostPathBreakdown(uint32_t aHostPathUs)
{
    uint32_t rcpNotifyUs   = 0;
    uint32_t hostRttUs     = 0;
    uint32_t rcpStackUs    = 0;
    uint32_t rcpUartPrepUs = 0;
    uint32_t rxProcUs      = 0;
    bool     haveNotify    = false;
    bool     haveHostRtt   = false;
    bool     haveStack     = false;
    bool     haveUartPrep  = false;
    bool     haveRxProc    = false;

    if (sNotifyUartTxDoneUs != 0 && sRadioTxDoneUs != 0 && sNotifyUartTxDoneUs >= sRadioTxDoneUs)
    {
        rcpNotifyUs = (uint32_t)(sNotifyUartTxDoneUs - sRadioTxDoneUs);
        haveNotify  = true;
    }

    if (sLastUartDeliverUs != 0 && sNotifyUartTxDoneUs != 0 && sLastUartDeliverUs >= sNotifyUartTxDoneUs)
    {
        hostRttUs   = (uint32_t)(sLastUartDeliverUs - sNotifyUartTxDoneUs);
        haveHostRtt = true;
    }

    if (sRadioProcessTxDoneUs != 0 && sRadioTxDoneUs != 0 && sRadioProcessTxDoneUs >= sRadioTxDoneUs)
    {
        rcpStackUs = (uint32_t)(sRadioProcessTxDoneUs - sRadioTxDoneUs);
        haveStack  = true;
    }

    if (sNotifyUartTxSendUs != 0 && sRadioProcessTxDoneUs != 0 && sNotifyUartTxSendUs >= sRadioProcessTxDoneUs)
    {
        rcpUartPrepUs = (uint32_t)(sNotifyUartTxSendUs - sRadioProcessTxDoneUs);
        haveUartPrep  = true;
    }

    if (sHostCmdEndRxIrqUs != 0 && sLastUartDeliverUs != 0 && sLastUartDeliverUs >= sHostCmdEndRxIrqUs)
    {
        rxProcUs   = (uint32_t)(sLastUartDeliverUs - sHostCmdEndRxIrqUs);
        haveRxProc = true;
    }

    (void)aHostPathUs;

    if (haveNotify)
    {
        recordBurstSample(&g_nrf54_rcp_tput_stats.burst_rcp_notify_min_us,
                          NULL,
                          &g_nrf54_rcp_tput_stats.burst_rcp_notify_sum_us,
                          &g_nrf54_rcp_tput_stats.burst_rcp_notify_count,
                          rcpNotifyUs);
    }

    if (haveHostRtt)
    {
        recordBurstSample(&g_nrf54_rcp_tput_stats.burst_host_rtt_min_us,
                          NULL,
                          &g_nrf54_rcp_tput_stats.burst_host_rtt_sum_us,
                          &g_nrf54_rcp_tput_stats.burst_host_rtt_count,
                          hostRttUs);
    }

    if (haveStack)
    {
        recordBurstSample(&g_nrf54_rcp_tput_stats.burst_rcp_stack_min_us,
                          NULL,
                          &g_nrf54_rcp_tput_stats.burst_rcp_stack_sum_us,
                          &g_nrf54_rcp_tput_stats.burst_rcp_stack_count,
                          rcpStackUs);
    }

    if (haveUartPrep)
    {
        recordBurstSample(&g_nrf54_rcp_tput_stats.burst_rcp_uart_prep_min_us,
                          NULL,
                          &g_nrf54_rcp_tput_stats.burst_rcp_uart_prep_sum_us,
                          &g_nrf54_rcp_tput_stats.burst_rcp_uart_prep_count,
                          rcpUartPrepUs);
    }

    if (haveRxProc)
    {
        recordBurstSample(&g_nrf54_rcp_tput_stats.burst_uart_rx_proc_min_us,
                          NULL,
                          &g_nrf54_rcp_tput_stats.burst_uart_rx_proc_sum_us,
                          &g_nrf54_rcp_tput_stats.burst_uart_rx_proc_count,
                          rxProcUs);
    }

    if (sNotifySpinelEndFrameUs != 0 && sRadioProcessTxDoneUs != 0 &&
        sNotifySpinelEndFrameUs >= sRadioProcessTxDoneUs)
    {
        recordBurstSample(&g_nrf54_rcp_tput_stats.burst_rcp_spinel_encode_min_us,
                          NULL,
                          &g_nrf54_rcp_tput_stats.burst_rcp_spinel_encode_sum_us,
                          &g_nrf54_rcp_tput_stats.burst_rcp_spinel_encode_count,
                          (uint32_t)(sNotifySpinelEndFrameUs - sRadioProcessTxDoneUs));
    }

    if (sNotifyEncodeAndSendUs != 0 && sNotifySpinelEndFrameUs != 0 &&
        sNotifyEncodeAndSendUs >= sNotifySpinelEndFrameUs)
    {
        recordBurstSample(&g_nrf54_rcp_tput_stats.burst_rcp_tasklet_defer_min_us,
                          NULL,
                          &g_nrf54_rcp_tput_stats.burst_rcp_tasklet_defer_sum_us,
                          &g_nrf54_rcp_tput_stats.burst_rcp_tasklet_defer_count,
                          (uint32_t)(sNotifyEncodeAndSendUs - sNotifySpinelEndFrameUs));
    }

    if (sNotifyUartTxSendUs != 0 && sNotifyEncodeAndSendUs != 0 && sNotifyUartTxSendUs >= sNotifyEncodeAndSendUs)
    {
        recordBurstSample(&g_nrf54_rcp_tput_stats.burst_rcp_hdlc_encode_min_us,
                          NULL,
                          &g_nrf54_rcp_tput_stats.burst_rcp_hdlc_encode_sum_us,
                          &g_nrf54_rcp_tput_stats.burst_rcp_hdlc_encode_count,
                          (uint32_t)(sNotifyUartTxSendUs - sNotifyEncodeAndSendUs));
    }
}

static void recordRadioTxLatencyHistogram(uint32_t aLatencyUs)
{
    if (aLatencyUs < 100u)
    {
        g_nrf54_rcp_tput_stats.radio_tx_lat_0_100++;
    }
    else if (aLatencyUs < 500u)
    {
        g_nrf54_rcp_tput_stats.radio_tx_lat_100_500++;
    }
    else if (aLatencyUs < 2000u)
    {
        g_nrf54_rcp_tput_stats.radio_tx_lat_500_2000++;
    }
    else
    {
        g_nrf54_rcp_tput_stats.radio_tx_lat_2000_plus++;
    }
}

static void recordLptimerSlipHistogram(uint32_t aSlipTicks)
{
    g_nrf54_rcp_tput_stats.lptimer_fires++;

    if (aSlipTicks < 10u)
    {
        g_nrf54_rcp_tput_stats.lptimer_slip_0_10++;
    }
    else if (aSlipTicks < 50u)
    {
        g_nrf54_rcp_tput_stats.lptimer_slip_10_50++;
    }
    else if (aSlipTicks < 200u)
    {
        g_nrf54_rcp_tput_stats.lptimer_slip_50_200++;
    }
    else
    {
        g_nrf54_rcp_tput_stats.lptimer_slip_200_plus++;
    }

    g_nrf54_rcp_tput_stats.lptimer_slip_sum_ticks += aSlipTicks;
    g_nrf54_rcp_tput_stats.lptimer_slip_count++;
}

void nrf54RcpTputStatsInit(void)
{
    nrf54RcpTputStatsReset();
}

void nrf54RcpTputStatsReset(void)
{
    g_nrf54_rcp_tput_stats.magic   = NRF54_RCP_TPUT_STATS_MAGIC;
    g_nrf54_rcp_tput_stats.version = NRF54_RCP_TPUT_STATS_VERSION;
    g_nrf54_rcp_tput_stats.lat_radio_csma_min_us              = UINT32_MAX;
    g_nrf54_rcp_tput_stats.lat_radio_done_to_tx_enter_min_us  = UINT32_MAX;
    g_nrf54_rcp_tput_stats.lat_host_path_min_us               = UINT32_MAX;
    g_nrf54_rcp_tput_stats.lat_uart_to_radio_tx_min_us        = UINT32_MAX;
    g_nrf54_rcp_tput_stats.lat_radio_done_to_uart_min_us      = UINT32_MAX;
    g_nrf54_rcp_tput_stats.burst_done_to_tx_enter_min_us      = UINT32_MAX;
    g_nrf54_rcp_tput_stats.burst_host_path_min_us             = UINT32_MAX;
    g_nrf54_rcp_tput_stats.burst_uart_to_radio_min_us         = UINT32_MAX;
    g_nrf54_rcp_tput_stats.burst_csma_min_us                  = UINT32_MAX;
    g_nrf54_rcp_tput_stats.burst_rcp_notify_min_us            = UINT32_MAX;
    g_nrf54_rcp_tput_stats.burst_rcp_stack_min_us             = UINT32_MAX;
    g_nrf54_rcp_tput_stats.burst_rcp_uart_prep_min_us         = UINT32_MAX;
    g_nrf54_rcp_tput_stats.burst_host_rtt_min_us               = UINT32_MAX;
    g_nrf54_rcp_tput_stats.burst_uart_rx_proc_min_us           = UINT32_MAX;
    g_nrf54_rcp_tput_stats.burst_rcp_spinel_encode_min_us      = UINT32_MAX;
    g_nrf54_rcp_tput_stats.burst_rcp_tasklet_defer_min_us      = UINT32_MAX;
    g_nrf54_rcp_tput_stats.burst_rcp_hdlc_encode_min_us        = UINT32_MAX;
    g_nrf54_rcp_tput_stats.uart_tx_busy                         = 0;

    sUartEndRxIrqUs           = 0;
    sLastUartDeliverUs        = 0;
    sRadioTxStartUs           = 0;
    sRadioTxFrameStartedUs    = 0;
    sRadioTxDoneUs            = 0;
    sRadioProcessTxDoneUs     = 0;
    sNotifyUartTxSendUs       = 0;
    sNotifyUartTxDoneUs       = 0;
    sNotifySpinelEndFrameUs   = 0;
    sNotifyEncodeAndSendUs    = 0;
    sHostCmdEndRxIrqUs        = 0;
    sAwaitingHostCmd          = false;
    sBurstInterFrame          = false;
}

void nrf54RcpTputStatsNoteUartEndRxIrq(void)
{
    sUartEndRxIrqUs = nrf54RcpTputStatsNowUs();
    g_nrf54_rcp_tput_stats.uart_endrx_irq++;

    if (sAwaitingHostCmd)
    {
        sHostCmdEndRxIrqUs = sUartEndRxIrqUs;
    }
}

void nrf54RcpTputStatsNoteUartProcessReceiveStart(void)
{
    uint64_t now = nrf54RcpTputStatsNowUs();
    uint64_t irq = sUartEndRxIrqUs;

    if (irq != 0 && now >= irq)
    {
        uint32_t lagUs = (uint32_t)(now - irq);

        nrf54RcpTputStatsRecordSumMax(&g_nrf54_rcp_tput_stats.uart_endrx_to_process_sum_us,
                                      &g_nrf54_rcp_tput_stats.uart_endrx_to_process_max_us,
                                      lagUs);
    }
}

void nrf54RcpTputStatsNoteUartDelivered(uint32_t aNumBytes)
{
    g_nrf54_rcp_tput_stats.uart_rx_bytes += aNumBytes;
    g_nrf54_rcp_tput_stats.uart_platreceive_calls++;
    sLastUartDeliverUs = nrf54RcpTputStatsNowUs();

    if (sAwaitingHostCmd)
    {
        sAwaitingHostCmd = false;
    }
}

void nrf54RcpTputStatsNoteUartTxSend(void)
{
    uint64_t now = nrf54RcpTputStatsNowUs();

    g_nrf54_rcp_tput_stats.uart_tx_send++;

    if (sRadioProcessTxDoneUs != 0 && sNotifyUartTxDoneUs == 0 && now >= sRadioProcessTxDoneUs &&
        sNotifyUartTxSendUs == 0)
    {
        sNotifyUartTxSendUs = now;
    }
    else if (sRadioTxDoneUs != 0 && sNotifyUartTxDoneUs == 0 && sRadioProcessTxDoneUs == 0 &&
             now >= sRadioTxDoneUs && sNotifyUartTxSendUs == 0)
    {
        sNotifyUartTxSendUs = now;
    }
}

void nrf54RcpTputStatsNoteUartTxReap(void)
{
    g_nrf54_rcp_tput_stats.uart_tx_reap++;
}

void nrf54RcpTputStatsNoteUartTxDone(void)
{
    uint64_t now = nrf54RcpTputStatsNowUs();

    g_nrf54_rcp_tput_stats.uart_tx_done++;

    if (sRadioTxDoneUs != 0 && now >= sRadioTxDoneUs)
    {
        recordLatencySampleMin(&g_nrf54_rcp_tput_stats.lat_radio_done_to_uart_min_us,
                               &g_nrf54_rcp_tput_stats.lat_radio_done_to_uart_max_us,
                               &g_nrf54_rcp_tput_stats.lat_radio_done_to_uart_sum_us,
                               &g_nrf54_rcp_tput_stats.lat_radio_done_to_uart_count,
                               (uint32_t)(now - sRadioTxDoneUs));
    }

    sNotifyUartTxDoneUs = now;
    sAwaitingHostCmd    = true;
    sHostCmdEndRxIrqUs  = 0;
}

void nrf54RcpTputStatsNoteMainLoopDriverPass(void)
{
    g_nrf54_rcp_tput_stats.main_loop_driver_pass++;
}

void nrf54RcpTputStatsNoteTransportPass1(void)
{
    g_nrf54_rcp_tput_stats.transport_pass1++;
}

void nrf54RcpTputStatsNoteTransportPass2(void)
{
    g_nrf54_rcp_tput_stats.transport_pass2++;
}

void nrf54RcpTputStatsNoteRadioTxEnter(bool aCsma)
{
    uint64_t now = nrf54RcpTputStatsNowUs();

    g_nrf54_rcp_tput_stats.radio_tx_enter++;

    if (aCsma)
    {
        g_nrf54_rcp_tput_stats.radio_tx_csma++;
    }
    else
    {
        g_nrf54_rcp_tput_stats.radio_tx_raw++;
    }

    sRadioTxStartUs        = now;
    sRadioTxFrameStartedUs = 0;
    sBurstInterFrame       = false;

    if (sRadioTxDoneUs != 0 && now >= sRadioTxDoneUs)
    {
        uint32_t interFrameUs = (uint32_t)(now - sRadioTxDoneUs);

        recordLatencySampleMin(&g_nrf54_rcp_tput_stats.lat_radio_done_to_tx_enter_min_us,
                               &g_nrf54_rcp_tput_stats.lat_radio_done_to_tx_enter_max_us,
                               &g_nrf54_rcp_tput_stats.lat_radio_done_to_tx_enter_sum_us,
                               &g_nrf54_rcp_tput_stats.lat_radio_done_to_tx_enter_count,
                               interFrameUs);

        if (interFrameUs <= NRF54_RCP_TPUT_BURST_INTER_FRAME_MAX_US)
        {
            sBurstInterFrame = true;

            recordBurstSample(&g_nrf54_rcp_tput_stats.burst_done_to_tx_enter_min_us,
                              &g_nrf54_rcp_tput_stats.burst_done_to_tx_enter_max_us,
                              &g_nrf54_rcp_tput_stats.burst_done_to_tx_enter_sum_us,
                              &g_nrf54_rcp_tput_stats.burst_done_to_tx_enter_count,
                              interFrameUs);
        }
    }

    if (sLastUartDeliverUs != 0 && now >= sLastUartDeliverUs)
    {
        uint32_t uartToRadioUs = (uint32_t)(now - sLastUartDeliverUs);

        recordLatencySampleMin(&g_nrf54_rcp_tput_stats.lat_uart_to_radio_tx_min_us,
                               &g_nrf54_rcp_tput_stats.lat_uart_to_radio_tx_max_us,
                               &g_nrf54_rcp_tput_stats.lat_uart_to_radio_tx_sum_us,
                               &g_nrf54_rcp_tput_stats.lat_uart_to_radio_tx_count,
                               uartToRadioUs);

        if (sBurstInterFrame)
        {
            recordBurstSample(&g_nrf54_rcp_tput_stats.burst_uart_to_radio_min_us,
                              NULL,
                              &g_nrf54_rcp_tput_stats.burst_uart_to_radio_sum_us,
                              &g_nrf54_rcp_tput_stats.burst_uart_to_radio_count,
                              uartToRadioUs);
        }
    }

    if (sRadioTxDoneUs != 0 && sLastUartDeliverUs != 0 && sLastUartDeliverUs >= sRadioTxDoneUs)
    {
        uint32_t hostPathUs = (uint32_t)(sLastUartDeliverUs - sRadioTxDoneUs);

        recordLatencySampleMin(&g_nrf54_rcp_tput_stats.lat_host_path_min_us,
                               &g_nrf54_rcp_tput_stats.lat_host_path_max_us,
                               &g_nrf54_rcp_tput_stats.lat_host_path_sum_us,
                               &g_nrf54_rcp_tput_stats.lat_host_path_count,
                               hostPathUs);

        if (sBurstInterFrame)
        {
            recordBurstSample(&g_nrf54_rcp_tput_stats.burst_host_path_min_us,
                              NULL,
                              &g_nrf54_rcp_tput_stats.burst_host_path_sum_us,
                              &g_nrf54_rcp_tput_stats.burst_host_path_count,
                              hostPathUs);
            recordBurstHostPathBreakdown(hostPathUs);
        }
    }
}

void nrf54RcpTputStatsNoteRadioTxFrameStarted(void)
{
    uint64_t now = nrf54RcpTputStatsNowUs();

    sRadioTxFrameStartedUs = now;

    if (sRadioTxStartUs != 0 && now >= sRadioTxStartUs)
    {
        uint32_t csmaUs = (uint32_t)(now - sRadioTxStartUs);

        recordLatencySampleMin(&g_nrf54_rcp_tput_stats.lat_radio_csma_min_us,
                               &g_nrf54_rcp_tput_stats.lat_radio_csma_max_us,
                               &g_nrf54_rcp_tput_stats.lat_radio_csma_sum_us,
                               &g_nrf54_rcp_tput_stats.lat_radio_csma_count,
                               csmaUs);
        recordCsmaHistogram(csmaUs);

        if (sBurstInterFrame)
        {
            recordBurstSample(&g_nrf54_rcp_tput_stats.burst_csma_min_us,
                              NULL,
                              &g_nrf54_rcp_tput_stats.burst_csma_sum_us,
                              &g_nrf54_rcp_tput_stats.burst_csma_count,
                              csmaUs);
        }
    }

    sBurstInterFrame = false;
}

void nrf54RcpTputStatsNoteRadioFemFromSleep(void)
{
    g_nrf54_rcp_tput_stats.radio_fem_from_sleep++;
}

void nrf54RcpTputStatsNoteRadioTxComplete(bool aOk, bool aBusyChannel)
{
    uint64_t now = nrf54RcpTputStatsNowUs();

    sRadioTxDoneUs         = now;
    sRadioProcessTxDoneUs  = 0;
    sNotifyUartTxSendUs    = 0;
    sNotifyUartTxDoneUs    = 0;
    sNotifySpinelEndFrameUs = 0;
    sNotifyEncodeAndSendUs = 0;
    sHostCmdEndRxIrqUs     = 0;
    sAwaitingHostCmd       = false;

    if (aOk)
    {
        g_nrf54_rcp_tput_stats.radio_tx_done_ok++;
    }
    else if (aBusyChannel)
    {
        g_nrf54_rcp_tput_stats.radio_tx_fail_busy++;
    }
    else
    {
        g_nrf54_rcp_tput_stats.radio_tx_fail_no_ack++;
    }

    if (sRadioTxStartUs != 0 && now >= sRadioTxStartUs)
    {
        uint32_t latencyUs = (uint32_t)(now - sRadioTxStartUs);

        recordLatencySample(&g_nrf54_rcp_tput_stats.lat_radio_tx_max_us,
                            &g_nrf54_rcp_tput_stats.lat_radio_tx_sum_us,
                            &g_nrf54_rcp_tput_stats.lat_radio_tx_count,
                            latencyUs);
        recordRadioTxLatencyHistogram(latencyUs);
    }

    if (sRadioTxFrameStartedUs != 0 && now >= sRadioTxFrameStartedUs)
    {
        recordLatencySample(&g_nrf54_rcp_tput_stats.lat_radio_onair_max_us,
                            &g_nrf54_rcp_tput_stats.lat_radio_onair_sum_us,
                            &g_nrf54_rcp_tput_stats.lat_radio_onair_count,
                            (uint32_t)(now - sRadioTxFrameStartedUs));
    }

    sRadioTxFrameStartedUs = 0;
}

void nrf54RcpTputStatsNoteRadioProcessTxDone(void)
{
    uint64_t now = nrf54RcpTputStatsNowUs();

    g_nrf54_rcp_tput_stats.radio_process_tx_done++;

    if (sRadioTxDoneUs != 0 && now >= sRadioTxDoneUs)
    {
        sRadioProcessTxDoneUs = now;
    }
}

void nrf54RcpTputStatsNoteLptimerFire(uint32_t aSlipTicks)
{
    recordLptimerSlipHistogram(aSlipTicks);
}

void nrf_802154_platform_rcp_tput_stats_tx_frame_started(void)
{
    nrf54RcpTputStatsNoteRadioTxFrameStarted();
}

void nrf_802154_platform_rcp_tput_stats_lptimer_fire(uint32_t aSlipTicks)
{
    nrf54RcpTputStatsNoteLptimerFire(aSlipTicks);
}

void nrf54RcpTputStatsNoteCsmaCcaBusy(void)
{
    g_nrf54_rcp_tput_stats.csma_cca_busy++;
}

void nrf54RcpTputStatsNoteCsmaBackoffScheduled(void)
{
    g_nrf54_rcp_tput_stats.csma_backoff_scheduled++;
}

void nrf54RcpTputStatsNoteCsmaOnAir(uint8_t aBackoffAttempts)
{
    if (aBackoffAttempts == 0u)
    {
        g_nrf54_rcp_tput_stats.csma_onair_nb_0++;
    }
    else if (aBackoffAttempts == 1u)
    {
        g_nrf54_rcp_tput_stats.csma_onair_nb_1++;
    }
    else
    {
        g_nrf54_rcp_tput_stats.csma_onair_nb_2plus++;
    }
}

void nrf54RcpTputStatsNoteLinkRawEndFrame(void)
{
    if (sRadioProcessTxDoneUs != 0 && sNotifyUartTxSendUs == 0 && sNotifySpinelEndFrameUs == 0)
    {
        sNotifySpinelEndFrameUs = nrf54RcpTputStatsNowUs();
    }
}

void nrf54RcpTputStatsNoteHdlcEncodeAndSendEnter(void)
{
    if (sRadioProcessTxDoneUs != 0 && sNotifyUartTxSendUs == 0 && sNotifyEncodeAndSendUs == 0)
    {
        sNotifyEncodeAndSendUs = nrf54RcpTputStatsNowUs();
    }
}

void nrf54RcpTputStatsNoteUartTxBusy(void)
{
    g_nrf54_rcp_tput_stats.uart_tx_busy++;
}

void nrf54_rcp_tput_ncp_note_link_raw_end_frame(void)
{
    nrf54RcpTputStatsNoteLinkRawEndFrame();
}

void nrf54_rcp_tput_ncp_note_hdlc_encode_and_send_enter(void)
{
    nrf54RcpTputStatsNoteHdlcEncodeAndSendEnter();
}

void nrf_802154_platform_rcp_tput_stats_csma_cca_busy(void)
{
    nrf54RcpTputStatsNoteCsmaCcaBusy();
}

void nrf_802154_platform_rcp_tput_stats_csma_backoff_scheduled(void)
{
    nrf54RcpTputStatsNoteCsmaBackoffScheduled();
}

void nrf_802154_platform_rcp_tput_stats_csma_on_air(uint8_t aBackoffAttempts)
{
    nrf54RcpTputStatsNoteCsmaOnAir(aBackoffAttempts);
}

static uint32_t avgUs(uint32_t sum, uint32_t count)
{
    return (count == 0u) ? 0u : (sum / count);
}

void nrf54RcpTputStatsDumpRtt(void)
{
    char     line[160];
    uint32_t passCount = g_nrf54_rcp_tput_stats.main_loop_driver_pass;

    SEGGER_RTT_WriteString(0, "\r\n=== nrf54 RCP tput stats ===\r\n");

    snprintf(line, sizeof(line), "driver_pass=%u transport1=%u transport2=%u\r\n", passCount,
             g_nrf54_rcp_tput_stats.transport_pass1, g_nrf54_rcp_tput_stats.transport_pass2);
    SEGGER_RTT_WriteString(0, line);

    snprintf(line, sizeof(line),
             "pass_total max/avg_us=%u/%u alarm=%u/%u t1=%u/%u radio=%u/%u t2=%u/%u\r\n",
             g_nrf54_rcp_tput_stats.pass_total_max_us, avgUs(g_nrf54_rcp_tput_stats.pass_total_sum_us, passCount),
             g_nrf54_rcp_tput_stats.pass_alarm_max_us, avgUs(g_nrf54_rcp_tput_stats.pass_alarm_sum_us, passCount),
             g_nrf54_rcp_tput_stats.pass_transport1_max_us,
             avgUs(g_nrf54_rcp_tput_stats.pass_transport1_sum_us, passCount),
             g_nrf54_rcp_tput_stats.pass_radio_max_us, avgUs(g_nrf54_rcp_tput_stats.pass_radio_sum_us, passCount),
             g_nrf54_rcp_tput_stats.pass_transport2_max_us,
             avgUs(g_nrf54_rcp_tput_stats.pass_transport2_sum_us, passCount));
    SEGGER_RTT_WriteString(0, line);

    snprintf(line, sizeof(line),
             "uart endrx=%u paused=%u bytes=%u endrx->proc max/avg=%u/%u\r\n",
             g_nrf54_rcp_tput_stats.uart_endrx_irq, g_nrf54_rcp_tput_stats.uart_rx_paused,
             g_nrf54_rcp_tput_stats.uart_rx_bytes, g_nrf54_rcp_tput_stats.uart_endrx_to_process_max_us,
             avgUs(g_nrf54_rcp_tput_stats.uart_endrx_to_process_sum_us, g_nrf54_rcp_tput_stats.uart_endrx_irq));
    SEGGER_RTT_WriteString(0, line);

    snprintf(line, sizeof(line),
             "lat uart->radio max/avg=%u/%u radio_tx max/avg=%u/%u done->uart max/avg=%u/%u\r\n",
             g_nrf54_rcp_tput_stats.lat_uart_to_radio_tx_max_us,
             avgUs(g_nrf54_rcp_tput_stats.lat_uart_to_radio_tx_sum_us,
                   g_nrf54_rcp_tput_stats.lat_uart_to_radio_tx_count),
             g_nrf54_rcp_tput_stats.lat_radio_tx_max_us,
             avgUs(g_nrf54_rcp_tput_stats.lat_radio_tx_sum_us, g_nrf54_rcp_tput_stats.lat_radio_tx_count),
             g_nrf54_rcp_tput_stats.lat_radio_done_to_uart_max_us,
             avgUs(g_nrf54_rcp_tput_stats.lat_radio_done_to_uart_sum_us,
                   g_nrf54_rcp_tput_stats.lat_radio_done_to_uart_count));
    SEGGER_RTT_WriteString(0, line);

    snprintf(line, sizeof(line),
             "lat done->tx_enter min/max/avg=%u/%u/%u csma min/max/avg=%u/%u/%u onair max/avg=%u/%u\r\n",
             g_nrf54_rcp_tput_stats.lat_radio_done_to_tx_enter_min_us == UINT32_MAX
                 ? 0u
                 : g_nrf54_rcp_tput_stats.lat_radio_done_to_tx_enter_min_us,
             g_nrf54_rcp_tput_stats.lat_radio_done_to_tx_enter_max_us,
             avgUs(g_nrf54_rcp_tput_stats.lat_radio_done_to_tx_enter_sum_us,
                   g_nrf54_rcp_tput_stats.lat_radio_done_to_tx_enter_count),
             g_nrf54_rcp_tput_stats.lat_radio_csma_min_us == UINT32_MAX ? 0u
                                                                        : g_nrf54_rcp_tput_stats.lat_radio_csma_min_us,
             g_nrf54_rcp_tput_stats.lat_radio_csma_max_us,
             avgUs(g_nrf54_rcp_tput_stats.lat_radio_csma_sum_us, g_nrf54_rcp_tput_stats.lat_radio_csma_count),
             g_nrf54_rcp_tput_stats.lat_radio_onair_max_us,
             avgUs(g_nrf54_rcp_tput_stats.lat_radio_onair_sum_us, g_nrf54_rcp_tput_stats.lat_radio_onair_count));
    SEGGER_RTT_WriteString(0, line);

    snprintf(line, sizeof(line),
             "host_path min/max/avg=%u/%u/%u csma_hist=[%u,%u,%u,%u,%u]\r\n",
             g_nrf54_rcp_tput_stats.lat_host_path_min_us == UINT32_MAX ? 0u
                                                                       : g_nrf54_rcp_tput_stats.lat_host_path_min_us,
             g_nrf54_rcp_tput_stats.lat_host_path_max_us,
             avgUs(g_nrf54_rcp_tput_stats.lat_host_path_sum_us, g_nrf54_rcp_tput_stats.lat_host_path_count),
             g_nrf54_rcp_tput_stats.csma_hist_0_320, g_nrf54_rcp_tput_stats.csma_hist_320_640,
             g_nrf54_rcp_tput_stats.csma_hist_640_1280, g_nrf54_rcp_tput_stats.csma_hist_1280_2000,
             g_nrf54_rcp_tput_stats.csma_hist_2000_plus);
    SEGGER_RTT_WriteString(0, line);

    snprintf(line, sizeof(line),
             "burst(<20ms) done->tx min/avg=%u/%u host min/avg=%u/%u uart->radio min/avg=%u/%u csma min/avg=%u/%u n=%u\r\n",
             g_nrf54_rcp_tput_stats.burst_done_to_tx_enter_min_us == UINT32_MAX
                 ? 0u
                 : g_nrf54_rcp_tput_stats.burst_done_to_tx_enter_min_us,
             avgUs(g_nrf54_rcp_tput_stats.burst_done_to_tx_enter_sum_us,
                   g_nrf54_rcp_tput_stats.burst_done_to_tx_enter_count),
             g_nrf54_rcp_tput_stats.burst_host_path_min_us == UINT32_MAX ? 0u
                                                                           : g_nrf54_rcp_tput_stats.burst_host_path_min_us,
             avgUs(g_nrf54_rcp_tput_stats.burst_host_path_sum_us, g_nrf54_rcp_tput_stats.burst_host_path_count),
             g_nrf54_rcp_tput_stats.burst_uart_to_radio_min_us == UINT32_MAX ? 0u
                                                                              : g_nrf54_rcp_tput_stats.burst_uart_to_radio_min_us,
             avgUs(g_nrf54_rcp_tput_stats.burst_uart_to_radio_sum_us,
                   g_nrf54_rcp_tput_stats.burst_uart_to_radio_count),
             g_nrf54_rcp_tput_stats.burst_csma_min_us == UINT32_MAX ? 0u
                                                                     : g_nrf54_rcp_tput_stats.burst_csma_min_us,
             avgUs(g_nrf54_rcp_tput_stats.burst_csma_sum_us, g_nrf54_rcp_tput_stats.burst_csma_count),
             g_nrf54_rcp_tput_stats.burst_done_to_tx_enter_count);
    SEGGER_RTT_WriteString(0, line);

    snprintf(line, sizeof(line),
             "host_path burst min us: notify=%u stack=%u uart_prep=%u host_rtt=%u rx_proc=%u\r\n",
             g_nrf54_rcp_tput_stats.burst_rcp_notify_min_us == UINT32_MAX
                 ? 0u
                 : g_nrf54_rcp_tput_stats.burst_rcp_notify_min_us,
             g_nrf54_rcp_tput_stats.burst_rcp_stack_min_us == UINT32_MAX ? 0u
                                                                          : g_nrf54_rcp_tput_stats.burst_rcp_stack_min_us,
             g_nrf54_rcp_tput_stats.burst_rcp_uart_prep_min_us == UINT32_MAX
                 ? 0u
                 : g_nrf54_rcp_tput_stats.burst_rcp_uart_prep_min_us,
             g_nrf54_rcp_tput_stats.burst_host_rtt_min_us == UINT32_MAX ? 0u
                                                                         : g_nrf54_rcp_tput_stats.burst_host_rtt_min_us,
             g_nrf54_rcp_tput_stats.burst_uart_rx_proc_min_us == UINT32_MAX
                 ? 0u
                 : g_nrf54_rcp_tput_stats.burst_uart_rx_proc_min_us);
    SEGGER_RTT_WriteString(0, line);

    snprintf(line, sizeof(line),
             "uart_prep v6 burst min: spinel=%u defer=%u hdlc=%u uart_tx_busy=%u\r\n",
             g_nrf54_rcp_tput_stats.burst_rcp_spinel_encode_min_us == UINT32_MAX
                 ? 0u
                 : g_nrf54_rcp_tput_stats.burst_rcp_spinel_encode_min_us,
             g_nrf54_rcp_tput_stats.burst_rcp_tasklet_defer_min_us == UINT32_MAX
                 ? 0u
                 : g_nrf54_rcp_tput_stats.burst_rcp_tasklet_defer_min_us,
             g_nrf54_rcp_tput_stats.burst_rcp_hdlc_encode_min_us == UINT32_MAX
                 ? 0u
                 : g_nrf54_rcp_tput_stats.burst_rcp_hdlc_encode_min_us,
             g_nrf54_rcp_tput_stats.uart_tx_busy);
    SEGGER_RTT_WriteString(0, line);

    snprintf(line, sizeof(line),
             "csma driver: cca_busy=%u backoff_sched=%u onair_nb=[%u,%u,%u]\r\n",
             g_nrf54_rcp_tput_stats.csma_cca_busy, g_nrf54_rcp_tput_stats.csma_backoff_scheduled,
             g_nrf54_rcp_tput_stats.csma_onair_nb_0, g_nrf54_rcp_tput_stats.csma_onair_nb_1,
             g_nrf54_rcp_tput_stats.csma_onair_nb_2plus);
    SEGGER_RTT_WriteString(0, line);

    snprintf(line, sizeof(line),
             "lptimer slip ticks avg=%u/%u hist=[%u,%u,%u,%u] fires=%u\r\n",
             g_nrf54_rcp_tput_stats.lptimer_slip_sum_ticks,
             avgUs(g_nrf54_rcp_tput_stats.lptimer_slip_sum_ticks, g_nrf54_rcp_tput_stats.lptimer_slip_count),
             g_nrf54_rcp_tput_stats.lptimer_slip_0_10, g_nrf54_rcp_tput_stats.lptimer_slip_10_50,
             g_nrf54_rcp_tput_stats.lptimer_slip_50_200, g_nrf54_rcp_tput_stats.lptimer_slip_200_plus,
             g_nrf54_rcp_tput_stats.lptimer_fires);
    SEGGER_RTT_WriteString(0, line);

    snprintf(line, sizeof(line),
             "radio tx=%u csma=%u ok=%u busy=%u noack=%u fem=%u hist=[%u,%u,%u,%u]\r\n",
             g_nrf54_rcp_tput_stats.radio_tx_enter, g_nrf54_rcp_tput_stats.radio_tx_csma,
             g_nrf54_rcp_tput_stats.radio_tx_done_ok, g_nrf54_rcp_tput_stats.radio_tx_fail_busy,
             g_nrf54_rcp_tput_stats.radio_tx_fail_no_ack, g_nrf54_rcp_tput_stats.radio_fem_from_sleep,
             g_nrf54_rcp_tput_stats.radio_tx_lat_0_100, g_nrf54_rcp_tput_stats.radio_tx_lat_100_500,
             g_nrf54_rcp_tput_stats.radio_tx_lat_500_2000, g_nrf54_rcp_tput_stats.radio_tx_lat_2000_plus);
    SEGGER_RTT_WriteString(0, line);

    SEGGER_RTT_WriteString(0, "=== end stats ===\r\n");
}

#endif // NRF54_RCP_TPUT_STATS
