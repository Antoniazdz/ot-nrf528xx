/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "nrf54_rcp_tput_stats.h"

#if NRF54_RCP_TPUT_STATS

#include "platform-nrf5.h"

#include <stdio.h>

#include <SEGGER_RTT.h>

nrf54_rcp_tput_stats_t g_nrf54_rcp_tput_stats;

static volatile uint64_t sUartEndRxIrqUs;
static uint64_t          sLastUartDeliverUs;
static uint64_t          sRadioTxStartUs;
static uint64_t          sRadioTxDoneUs;

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

void nrf54RcpTputStatsInit(void)
{
    nrf54RcpTputStatsReset();
}

void nrf54RcpTputStatsReset(void)
{
    g_nrf54_rcp_tput_stats.magic   = NRF54_RCP_TPUT_STATS_MAGIC;
    g_nrf54_rcp_tput_stats.version = NRF54_RCP_TPUT_STATS_VERSION;

    sUartEndRxIrqUs    = 0;
    sLastUartDeliverUs = 0;
    sRadioTxStartUs    = 0;
    sRadioTxDoneUs     = 0;
}

void nrf54RcpTputStatsNoteUartEndRxIrq(void)
{
    sUartEndRxIrqUs = nrf54RcpTputStatsNowUs();
    g_nrf54_rcp_tput_stats.uart_endrx_irq++;
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
}

void nrf54RcpTputStatsNoteUartTxSend(void)
{
    g_nrf54_rcp_tput_stats.uart_tx_send++;
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
        recordLatencySample(&g_nrf54_rcp_tput_stats.lat_radio_done_to_uart_max_us,
                            &g_nrf54_rcp_tput_stats.lat_radio_done_to_uart_sum_us,
                            &g_nrf54_rcp_tput_stats.lat_radio_done_to_uart_count,
                            (uint32_t)(now - sRadioTxDoneUs));
    }
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

    sRadioTxStartUs = now;

    if (sLastUartDeliverUs != 0 && now >= sLastUartDeliverUs)
    {
        recordLatencySample(&g_nrf54_rcp_tput_stats.lat_uart_to_radio_tx_max_us,
                            &g_nrf54_rcp_tput_stats.lat_uart_to_radio_tx_sum_us,
                            &g_nrf54_rcp_tput_stats.lat_uart_to_radio_tx_count,
                            (uint32_t)(now - sLastUartDeliverUs));
    }
}

void nrf54RcpTputStatsNoteRadioFemFromSleep(void)
{
    g_nrf54_rcp_tput_stats.radio_fem_from_sleep++;
}

void nrf54RcpTputStatsNoteRadioTxComplete(bool aOk, bool aBusyChannel)
{
    uint64_t now = nrf54RcpTputStatsNowUs();

    sRadioTxDoneUs = now;

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
}

void nrf54RcpTputStatsNoteRadioProcessTxDone(void)
{
    g_nrf54_rcp_tput_stats.radio_process_tx_done++;
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
