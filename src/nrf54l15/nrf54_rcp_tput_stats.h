/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  RCP throughput / latency counters for nRF54L15 bare-metal iperf tuning.
 *
 *  Read after an iperf run via GDB/nrfjprog:
 *    p g_nrf54_rcp_tput_stats
 *    p/x g_nrf54_rcp_tput_stats.lat_radio_tx_max_us
 *
 *  Or RTT dump (Debug builds): call nrf54RcpTputStatsDumpRtt() from GDB.
 *
 *  Layout is mirrored in test-fw nrf54_rcp_tput_stats.py (NRF54_RCP_TPUT_STATS_FIELDS).
 */

#ifndef NRF54_RCP_TPUT_STATS_H_
#define NRF54_RCP_TPUT_STATS_H_

#include <stdbool.h>
#include <stdint.h>

#ifndef NRF54_RCP_TPUT_STATS
#define NRF54_RCP_TPUT_STATS 1
#endif

#define NRF54_RCP_TPUT_STATS_MAGIC   0x52535054u /* 'RSPT' */
#define NRF54_RCP_TPUT_STATS_VERSION 1u

typedef struct
{
    uint32_t magic;
    uint32_t version;

    /* UART / Spinel ingress */
    uint32_t uart_endrx_irq;
    uint32_t uart_endtx_irq;
    uint32_t uart_rx_paused;
    uint32_t uart_rx_bytes;
    uint32_t uart_platreceive_calls;
    uint32_t uart_endrx_to_process_max_us;
    uint32_t uart_endrx_to_process_sum_us;
    uint32_t uart_tx_send;
    uint32_t uart_tx_reap;
    uint32_t uart_tx_done;

    /* Main loop / transport passes */
    uint32_t main_loop_driver_pass;
    uint32_t transport_pass1;
    uint32_t transport_pass2;

    /* otSysProcessDrivers phase durations (max and sum for average) */
    uint32_t pass_total_max_us;
    uint32_t pass_total_sum_us;
    uint32_t pass_alarm_max_us;
    uint32_t pass_alarm_sum_us;
    uint32_t pass_transport1_max_us;
    uint32_t pass_transport1_sum_us;
    uint32_t pass_radio_max_us;
    uint32_t pass_radio_sum_us;
    uint32_t pass_transport2_max_us;
    uint32_t pass_transport2_sum_us;

    /* Pipeline latency segments (microseconds) */
    uint32_t lat_uart_to_radio_tx_max_us;
    uint32_t lat_uart_to_radio_tx_sum_us;
    uint32_t lat_uart_to_radio_tx_count;
    uint32_t lat_radio_tx_max_us;
    uint32_t lat_radio_tx_sum_us;
    uint32_t lat_radio_tx_count;
    uint32_t lat_radio_done_to_uart_max_us;
    uint32_t lat_radio_done_to_uart_sum_us;
    uint32_t lat_radio_done_to_uart_count;

    /* Radio / CSMA / FEM */
    uint32_t radio_tx_enter;
    uint32_t radio_tx_csma;
    uint32_t radio_tx_raw;
    uint32_t radio_tx_done_ok;
    uint32_t radio_tx_fail_no_ack;
    uint32_t radio_tx_fail_busy;
    uint32_t radio_fem_from_sleep;
    uint32_t radio_process_tx_done;

    /* otPlatRadioTransmit → driver TX complete (CSMA + on-air), histogram */
    uint32_t radio_tx_lat_0_100;
    uint32_t radio_tx_lat_100_500;
    uint32_t radio_tx_lat_500_2000;
    uint32_t radio_tx_lat_2000_plus;
} nrf54_rcp_tput_stats_t;

extern nrf54_rcp_tput_stats_t g_nrf54_rcp_tput_stats;

void nrf54RcpTputStatsInit(void);
void nrf54RcpTputStatsReset(void);
void nrf54RcpTputStatsDumpRtt(void);

uint64_t nrf54RcpTputStatsNowUs(void);

void nrf54RcpTputStatsRecordMax(uint32_t *aMaxField, uint32_t aValueUs);
void nrf54RcpTputStatsRecordSumMax(uint32_t *aSumField, uint32_t *aMaxField, uint32_t aValueUs);

void nrf54RcpTputStatsNoteUartEndRxIrq(void);
void nrf54RcpTputStatsNoteUartProcessReceiveStart(void);
void nrf54RcpTputStatsNoteUartDelivered(uint32_t aNumBytes);
void nrf54RcpTputStatsNoteUartTxSend(void);
void nrf54RcpTputStatsNoteUartTxReap(void);
void nrf54RcpTputStatsNoteUartTxDone(void);

void nrf54RcpTputStatsNoteMainLoopDriverPass(void);
void nrf54RcpTputStatsNoteTransportPass1(void);
void nrf54RcpTputStatsNoteTransportPass2(void);

void nrf54RcpTputStatsNoteRadioTxEnter(bool aCsma);
void nrf54RcpTputStatsNoteRadioFemFromSleep(void);
void nrf54RcpTputStatsNoteRadioTxComplete(bool aOk, bool aBusyChannel);
void nrf54RcpTputStatsNoteRadioProcessTxDone(void);

#if !NRF54_RCP_TPUT_STATS

static inline void nrf54RcpTputStatsInit(void) {}
static inline void nrf54RcpTputStatsReset(void) {}
static inline void nrf54RcpTputStatsDumpRtt(void) {}
static inline uint64_t nrf54RcpTputStatsNowUs(void) { return 0; }
static inline void nrf54RcpTputStatsRecordMax(uint32_t *aMaxField, uint32_t aValueUs)
{
    (void)aMaxField;
    (void)aValueUs;
}
static inline void nrf54RcpTputStatsRecordSumMax(uint32_t *aSumField, uint32_t *aMaxField, uint32_t aValueUs)
{
    (void)aSumField;
    (void)aMaxField;
    (void)aValueUs;
}
static inline void nrf54RcpTputStatsNoteUartEndRxIrq(void) {}
static inline void nrf54RcpTputStatsNoteUartProcessReceiveStart(void) {}
static inline void nrf54RcpTputStatsNoteUartDelivered(uint32_t aNumBytes) { (void)aNumBytes; }
static inline void nrf54RcpTputStatsNoteUartTxSend(void) {}
static inline void nrf54RcpTputStatsNoteUartTxReap(void) {}
static inline void nrf54RcpTputStatsNoteUartTxDone(void) {}
static inline void nrf54RcpTputStatsNoteMainLoopDriverPass(void) {}
static inline void nrf54RcpTputStatsNoteTransportPass1(void) {}
static inline void nrf54RcpTputStatsNoteTransportPass2(void) {}
static inline void nrf54RcpTputStatsNoteRadioTxEnter(bool aCsma) { (void)aCsma; }
static inline void nrf54RcpTputStatsNoteRadioFemFromSleep(void) {}
static inline void nrf54RcpTputStatsNoteRadioTxComplete(bool aOk, bool aBusyChannel)
{
    (void)aOk;
    (void)aBusyChannel;
}
static inline void nrf54RcpTputStatsNoteRadioProcessTxDone(void) {}

#endif // !NRF54_RCP_TPUT_STATS

#endif // NRF54_RCP_TPUT_STATS_H_
