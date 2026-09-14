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
 *  v6: 118 x uint32 = 472 B (+ uart_prep split: spinel / tasklet / hdlc).
 */

#ifndef NRF54_RCP_TPUT_STATS_H_
#define NRF54_RCP_TPUT_STATS_H_

#include <stdbool.h>
#include <stdint.h>

#ifndef NRF54_RCP_TPUT_STATS
#define NRF54_RCP_TPUT_STATS 1
#endif

#define NRF54_RCP_TPUT_STATS_MAGIC   0x52535054u /* 'RSPT' */
#define NRF54_RCP_TPUT_STATS_VERSION 6u

/** Inter-frame gaps below this (us) are treated as active iperf burst traffic. */
#define NRF54_RCP_TPUT_BURST_INTER_FRAME_MAX_US 20000u

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
    uint32_t lat_uart_to_radio_tx_min_us;
    uint32_t lat_uart_to_radio_tx_max_us;
    uint32_t lat_uart_to_radio_tx_sum_us;
    uint32_t lat_uart_to_radio_tx_count;
    uint32_t lat_radio_tx_max_us;
    uint32_t lat_radio_tx_sum_us;
    uint32_t lat_radio_tx_count;
    uint32_t lat_radio_done_to_uart_min_us;
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

    /* Previous TX complete → next TX enter (inter-frame pipeline; PCAP floor proxy) */
    uint32_t lat_radio_done_to_tx_enter_min_us;
    uint32_t lat_radio_done_to_tx_enter_max_us;
    uint32_t lat_radio_done_to_tx_enter_sum_us;
    uint32_t lat_radio_done_to_tx_enter_count;

    /* TX complete → UART deliver of next host command (host RTT + RCP Spinel TX) */
    uint32_t lat_host_path_min_us;
    uint32_t lat_host_path_max_us;
    uint32_t lat_host_path_sum_us;
    uint32_t lat_host_path_count;

    /* TX enter → SHR on air (CSMA-CA + channel access before on-air) */
    uint32_t lat_radio_csma_min_us;
    uint32_t lat_radio_csma_max_us;
    uint32_t lat_radio_csma_sum_us;
    uint32_t lat_radio_csma_count;

    /* SHR on air → TX complete (on-air + ACK wait) */
    uint32_t lat_radio_onair_max_us;
    uint32_t lat_radio_onair_sum_us;
    uint32_t lat_radio_onair_count;

    /* CSMA duration histogram (microseconds, tx_enter → SHR) */
    uint32_t csma_hist_0_320;
    uint32_t csma_hist_320_640;
    uint32_t csma_hist_640_1280;
    uint32_t csma_hist_1280_2000;
    uint32_t csma_hist_2000_plus;

    /*
     * Active-burst inter-frame stats (done→tx_enter < 20 ms).
     * Excludes long idle gaps; correlates with PCAP iperf window.
     * burst_done_to_tx_enter + burst_csma ≈ PCAP gap during burst.
     */
    uint32_t burst_done_to_tx_enter_min_us;
    uint32_t burst_done_to_tx_enter_max_us;
    uint32_t burst_done_to_tx_enter_sum_us;
    uint32_t burst_done_to_tx_enter_count;
    uint32_t burst_host_path_min_us;
    uint32_t burst_host_path_sum_us;
    uint32_t burst_host_path_count;
    uint32_t burst_uart_to_radio_min_us;
    uint32_t burst_uart_to_radio_sum_us;
    uint32_t burst_uart_to_radio_count;
    uint32_t burst_csma_min_us;
    uint32_t burst_csma_sum_us;
    uint32_t burst_csma_count;

    /*
     * burst_host_path decomposition (v5, burst inter-frame < 20 ms):
     *   host_path ≈ rcp_notify + host_rtt
     *   rcp_notify ≈ rcp_stack + rcp_uart_prep
     *   host_rtt ≈ wire/host/ot-daemon RTT; uart_rx_proc is RCP-only RX decode lag.
     */
    uint32_t burst_rcp_notify_min_us;     /* radio TX done -> UART TX done (Spinel notify egress) */
    uint32_t burst_rcp_notify_sum_us;
    uint32_t burst_rcp_notify_count;
    uint32_t burst_rcp_stack_min_us;      /* radio TX done -> otPlatRadioTxDone processed */
    uint32_t burst_rcp_stack_sum_us;
    uint32_t burst_rcp_stack_count;
    uint32_t burst_rcp_uart_prep_min_us;  /* process TX done -> otPlatUartSend (encode/queue) */
    uint32_t burst_rcp_uart_prep_sum_us;
    uint32_t burst_rcp_uart_prep_count;
    uint32_t burst_host_rtt_min_us;       /* UART TX done -> next cmd UART deliver (host RTT) */
    uint32_t burst_host_rtt_sum_us;
    uint32_t burst_host_rtt_count;
    uint32_t burst_uart_rx_proc_min_us;   /* ENDRX IRQ -> UART deliver for next cmd */
    uint32_t burst_uart_rx_proc_sum_us;
    uint32_t burst_uart_rx_proc_count;

    /*
     * burst_rcp_uart_prep split (v6, burst inter-frame < 20 ms):
     *   uart_prep ≈ spinel_encode + tasklet_defer + hdlc_encode
     */
    uint32_t burst_rcp_spinel_encode_min_us;  /* process_tx_done -> LinkRaw EndFrame */
    uint32_t burst_rcp_spinel_encode_sum_us;
    uint32_t burst_rcp_spinel_encode_count;
    uint32_t burst_rcp_tasklet_defer_min_us;  /* EndFrame -> EncodeAndSend entry */
    uint32_t burst_rcp_tasklet_defer_sum_us;
    uint32_t burst_rcp_tasklet_defer_count;
    uint32_t burst_rcp_hdlc_encode_min_us;    /* EncodeAndSend entry -> otPlatUartSend */
    uint32_t burst_rcp_hdlc_encode_sum_us;
    uint32_t burst_rcp_hdlc_encode_count;
    uint32_t uart_tx_busy;                    /* otPlatUartSend returned OT_ERROR_BUSY */

    /* CSMA-CA driver events (from nrf_802154_csma_ca / core CCA) */
    uint32_t csma_cca_busy;
    uint32_t csma_backoff_scheduled;
    uint32_t csma_onair_nb_0;
    uint32_t csma_onair_nb_1;
    uint32_t csma_onair_nb_2plus;

    /* SL lptimer CC slip at fire: curr_ticks - scheduled_fire (1 tick = 1 us) */
    uint32_t lptimer_slip_0_10;
    uint32_t lptimer_slip_10_50;
    uint32_t lptimer_slip_50_200;
    uint32_t lptimer_slip_200_plus;
    uint32_t lptimer_slip_sum_ticks;
    uint32_t lptimer_slip_count;
    uint32_t lptimer_fires;
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
void nrf54RcpTputStatsNoteRadioTxFrameStarted(void);
void nrf54RcpTputStatsNoteLptimerFire(uint32_t aSlipTicks);
void nrf54RcpTputStatsNoteCsmaCcaBusy(void);
void nrf54RcpTputStatsNoteCsmaBackoffScheduled(void);
void nrf54RcpTputStatsNoteCsmaOnAir(uint8_t aBackoffAttempts);

void nrf54RcpTputStatsNoteLinkRawEndFrame(void);
void nrf54RcpTputStatsNoteHdlcEncodeAndSendEnter(void);
void nrf54RcpTputStatsNoteUartTxBusy(void);

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
static inline void nrf54RcpTputStatsNoteRadioTxFrameStarted(void) {}
static inline void nrf54RcpTputStatsNoteLptimerFire(uint32_t aSlipTicks) { (void)aSlipTicks; }
static inline void nrf54RcpTputStatsNoteCsmaCcaBusy(void) {}
static inline void nrf54RcpTputStatsNoteCsmaBackoffScheduled(void) {}
static inline void nrf54RcpTputStatsNoteCsmaOnAir(uint8_t aBackoffAttempts) { (void)aBackoffAttempts; }
static inline void nrf54RcpTputStatsNoteLinkRawEndFrame(void) {}
static inline void nrf54RcpTputStatsNoteHdlcEncodeAndSendEnter(void) {}
static inline void nrf54RcpTputStatsNoteUartTxBusy(void) {}

#endif // !NRF54_RCP_TPUT_STATS

#endif // NRF54_RCP_TPUT_STATS_H_
