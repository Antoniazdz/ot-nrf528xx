/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Throughput-path timing counters for bare-metal vs NCS comparison.
 *  Active while a perf client run is in progress through radio-queue drain.
 */

#ifndef OT_PERF_TIMING_H_
#define OT_PERF_TIMING_H_

#include <stdbool.h>
#include <stdint.h>

#include <openthread/instance.h>
#include <openthread/platform/radio.h>

/** First 6LoWPAN MAC fragment in a 100 B UDP datagram is ~120 B; second is ~36 B. */
#define OT_PERF_TIMING_LARGE_FRAG_THRESHOLD 64

typedef struct OtPerfTimingStats
{
    /* Bare-metal otSysProcessDrivers breakdown (system_nrf54.c). */
    uint32_t driver_pass_count;
    uint32_t transport_us_total;
    uint32_t transport_us_max;
    uint32_t transport_calls;
    uint32_t perf_process_us_total;
    uint32_t perf_process_us_max;
    uint32_t perf_process_calls;
    uint32_t radio_pre_us_total;
    uint32_t radio_pre_us_max;
    uint32_t radio_pre_calls;
    uint32_t radio_post_us_total;
    uint32_t radio_post_us_max;
    uint32_t radio_post_calls;

    /* NCS / fallback: total otSysProcessDrivers when not using BM breakdown. */
    uint32_t drivers_total_us;
    uint32_t drivers_total_us_max;
    uint32_t drivers_total_calls;

    /* NCS pump thread (ot_perf.c PumpThread). */
    uint32_t pump_thread_us_total;
    uint32_t pump_thread_us_max;
    uint32_t pump_thread_calls;

    /* perf client injection (ClientSendOne). */
    uint32_t send_one_us_total;
    uint32_t send_one_us_max;
    uint32_t send_one_calls;

    /* Inter-frame software gap: otPlatRadioTxDone -> next otPlatRadioTransmit. */
    uint32_t tx_done_count;
    uint32_t tx_submit_count;
    uint32_t gap_us_total;
    uint32_t gap_us_max;
    uint32_t gap_submit_large_count;
    uint32_t gap_submit_large_us_total;
    uint32_t gap_submit_small_count;
    uint32_t gap_submit_small_us_total;
    uint32_t gap_bucket_0_500us;
    uint32_t gap_bucket_500_1000us;
    uint32_t gap_bucket_1_2ms;
    uint32_t gap_bucket_2_5ms;
    uint32_t gap_bucket_5ms_plus;
} OtPerfTimingStats;

void otPerfTimingReset(void);
void otPerfTimingSetActive(bool aActive);
bool otPerfTimingIsActive(void);

const OtPerfTimingStats *otPerfTimingGetStats(void);

void otPerfTimingReport(void);

/* Bare-metal driver pass hooks (system_nrf54.c). */
void otPerfTimingDriverPassBegin(void);
void otPerfTimingAddTransportUs(uint32_t aMicroseconds);
void otPerfTimingAddPerfProcessUs(uint32_t aMicroseconds);
void otPerfTimingAddRadioPreUs(uint32_t aMicroseconds);
void otPerfTimingAddRadioPostUs(uint32_t aMicroseconds);
void otPerfTimingDriverPassEnd(void);

/* NCS / generic hooks. */
void otPerfTimingAddDriversTotalUs(uint32_t aMicroseconds);
void otPerfTimingAddPumpThreadUs(uint32_t aMicroseconds);
void otPerfTimingAddSendOneUs(uint32_t aMicroseconds);

/* Called from linker wraps around otPlatRadioTxDone / otPlatRadioTransmit. */
void otPerfTimingOnTxDone(const otRadioFrame *aFrame);
void otPerfTimingOnTxSubmit(const otRadioFrame *aFrame);

/* Millisecond alarm time of the most recent otPlatRadioTxDone (0 if none yet). */
uint32_t otPerfTimingGetLastTxDoneAlarmMs(void);

/* Linker wraps (both platforms when --wrap is enabled). */
void __wrap_otPlatRadioTxDone(otInstance       *aInstance,
                              otRadioFrame     *aFrame,
                              otRadioFrame     *aAckFrame,
                              otError           aError);
void __wrap_otPlatRadioTransmit(otInstance *aInstance, otRadioFrame *aFrame);
void __wrap_otSysProcessDrivers(otInstance *aInstance);

#endif // OT_PERF_TIMING_H_
