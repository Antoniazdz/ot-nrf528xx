/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "ot_perf_timing.h"

#include <string.h>

#include <openthread/cli.h>
#include <openthread/platform/alarm-milli.h>
#include <openthread/platform/time.h>

static volatile bool           sActive;
static uint32_t                sLastTxDoneAlarmMs;
static OtPerfTimingStats       sStats;
static uint64_t                sLastTxDoneUs;
static bool                    sHadTxDone;
static uint32_t                sDriverPassTransportUs;
static uint32_t                sDriverPassPerfUs;
static uint32_t                sDriverPassRadioPreUs;
static uint32_t                sDriverPassRadioPostUs;

extern void __real_otPlatRadioTxDone(otInstance   *aInstance,
                                     otRadioFrame *aFrame,
                                     otRadioFrame *aAckFrame,
                                     otError       aError);
extern void __real_otPlatRadioTransmit(otInstance *aInstance, otRadioFrame *aFrame);
extern void __real_otSysProcessDrivers(otInstance *aInstance);

static void AddMax(uint32_t *aMax, uint32_t aValue)
{
    if (aValue > *aMax)
    {
        *aMax = aValue;
    }
}

static void RecordGapUs(uint32_t aGapUs, uint16_t aSubmitLength)
{
    sStats.gap_us_total += aGapUs;
    AddMax(&sStats.gap_us_max, aGapUs);

    if (aSubmitLength > OT_PERF_TIMING_LARGE_FRAG_THRESHOLD)
    {
        sStats.gap_submit_large_count++;
        sStats.gap_submit_large_us_total += aGapUs;
    }
    else
    {
        sStats.gap_submit_small_count++;
        sStats.gap_submit_small_us_total += aGapUs;
    }

    if (aGapUs < 500U)
    {
        sStats.gap_bucket_0_500us++;
    }
    else if (aGapUs < 1000U)
    {
        sStats.gap_bucket_500_1000us++;
    }
    else if (aGapUs < 2000U)
    {
        sStats.gap_bucket_1_2ms++;
    }
    else if (aGapUs < 5000U)
    {
        sStats.gap_bucket_2_5ms++;
    }
    else
    {
        sStats.gap_bucket_5ms_plus++;
    }
}

static uint32_t ElapsedUs(uint64_t aStart, uint64_t aEnd)
{
    return (uint32_t)((aEnd >= aStart) ? (aEnd - aStart) : 0U);
}

void otPerfTimingReset(void)
{
    memset(&sStats, 0, sizeof(sStats));
    sLastTxDoneUs       = 0;
    sLastTxDoneAlarmMs  = 0;
    sHadTxDone          = false;
    sDriverPassTransportUs  = 0;
    sDriverPassPerfUs       = 0;
    sDriverPassRadioPreUs   = 0;
    sDriverPassRadioPostUs  = 0;
}

void otPerfTimingSetActive(bool aActive) { sActive = aActive; }

bool otPerfTimingIsActive(void) { return sActive; }

const OtPerfTimingStats *otPerfTimingGetStats(void) { return &sStats; }

void otPerfTimingDriverPassBegin(void)
{
    if (!sActive)
    {
        return;
    }

    sDriverPassTransportUs = 0;
    sDriverPassPerfUs      = 0;
    sDriverPassRadioPreUs  = 0;
    sDriverPassRadioPostUs = 0;
}

void otPerfTimingAddTransportUs(uint32_t aMicroseconds)
{
    if (!sActive)
    {
        return;
    }

    sDriverPassTransportUs += aMicroseconds;
    sStats.transport_us_total += aMicroseconds;
    AddMax(&sStats.transport_us_max, aMicroseconds);
    sStats.transport_calls++;
}

void otPerfTimingAddPerfProcessUs(uint32_t aMicroseconds)
{
    if (!sActive)
    {
        return;
    }

    sDriverPassPerfUs += aMicroseconds;
    sStats.perf_process_us_total += aMicroseconds;
    AddMax(&sStats.perf_process_us_max, aMicroseconds);
    sStats.perf_process_calls++;
}

void otPerfTimingAddRadioPreUs(uint32_t aMicroseconds)
{
    if (!sActive)
    {
        return;
    }

    sDriverPassRadioPreUs += aMicroseconds;
    sStats.radio_pre_us_total += aMicroseconds;
    AddMax(&sStats.radio_pre_us_max, aMicroseconds);
    sStats.radio_pre_calls++;
}

void otPerfTimingAddRadioPostUs(uint32_t aMicroseconds)
{
    if (!sActive)
    {
        return;
    }

    sDriverPassRadioPostUs += aMicroseconds;
    sStats.radio_post_us_total += aMicroseconds;
    AddMax(&sStats.radio_post_us_max, aMicroseconds);
    sStats.radio_post_calls++;
}

void otPerfTimingDriverPassEnd(void)
{
    if (!sActive)
    {
        return;
    }

    sStats.driver_pass_count++;
}

void otPerfTimingAddDriversTotalUs(uint32_t aMicroseconds)
{
    if (!sActive)
    {
        return;
    }

    sStats.drivers_total_us += aMicroseconds;
    AddMax(&sStats.drivers_total_us_max, aMicroseconds);
    sStats.drivers_total_calls++;
}

void otPerfTimingAddPumpThreadUs(uint32_t aMicroseconds)
{
    if (!sActive)
    {
        return;
    }

    sStats.pump_thread_us_total += aMicroseconds;
    AddMax(&sStats.pump_thread_us_max, aMicroseconds);
    sStats.pump_thread_calls++;
}

void otPerfTimingAddSendOneUs(uint32_t aMicroseconds)
{
    if (!sActive)
    {
        return;
    }

    sStats.send_one_us_total += aMicroseconds;
    AddMax(&sStats.send_one_us_max, aMicroseconds);
    sStats.send_one_calls++;
}

void otPerfTimingOnTxDone(const otRadioFrame *aFrame)
{
    if (!sActive || aFrame == NULL)
    {
        return;
    }

    sStats.tx_done_count++;
    sLastTxDoneUs      = otPlatTimeGet();
    sLastTxDoneAlarmMs = otPlatAlarmMilliGetNow();
    sHadTxDone         = true;
}

uint32_t otPerfTimingGetLastTxDoneAlarmMs(void) { return sLastTxDoneAlarmMs; }

void otPerfTimingOnTxSubmit(const otRadioFrame *aFrame)
{
    uint64_t now;
    uint32_t gapUs;

    if (!sActive || aFrame == NULL || !sHadTxDone)
    {
        return;
    }

    now   = otPlatTimeGet();
    gapUs = ElapsedUs(sLastTxDoneUs, now);

    sStats.tx_submit_count++;
    RecordGapUs(gapUs, aFrame->mLength);
    sHadTxDone = false;
}

void __wrap_otPlatRadioTxDone(otInstance   *aInstance,
                              otRadioFrame *aFrame,
                              otRadioFrame *aAckFrame,
                              otError       aError)
{
    otPerfTimingOnTxDone(aFrame);
    __real_otPlatRadioTxDone(aInstance, aFrame, aAckFrame, aError);
}

void __wrap_otPlatRadioTransmit(otInstance *aInstance, otRadioFrame *aFrame)
{
    otPerfTimingOnTxSubmit(aFrame);
    __real_otPlatRadioTransmit(aInstance, aFrame);
}

void __wrap_otSysProcessDrivers(otInstance *aInstance)
{
    if (sActive)
    {
        uint64_t start = otPlatTimeGet();

        __real_otSysProcessDrivers(aInstance);
        otPerfTimingAddDriversTotalUs(ElapsedUs(start, otPlatTimeGet()));
    }
    else
    {
        __real_otSysProcessDrivers(aInstance);
    }
}

static void OutputAvgMax(const char *aLabel, uint32_t aTotalUs, uint32_t aMaxUs, uint32_t aCount)
{
    if (aCount == 0U)
    {
        otCliOutputFormat("%s avg_us=- max_us=- calls=%u\r\n", aLabel, (unsigned)aCount);
    }
    else
    {
        otCliOutputFormat("%s avg_us=%u max_us=%u calls=%u\r\n", aLabel, (unsigned)(aTotalUs / aCount),
                        (unsigned)aMaxUs, (unsigned)aCount);
    }
}

void otPerfTimingReport(void)
{
    const OtPerfTimingStats *stats = otPerfTimingGetStats();

    otCliOutputFormat("perf: timing active=%u\r\n", (unsigned)sActive);

    otCliOutputFormat("perf: timing driver_pass=%u\r\n", (unsigned)stats->driver_pass_count);
    OutputAvgMax("perf: timing transport", stats->transport_us_total, stats->transport_us_max, stats->transport_calls);
    OutputAvgMax("perf: timing radio_pre", stats->radio_pre_us_total, stats->radio_pre_us_max, stats->radio_pre_calls);
    OutputAvgMax("perf: timing perf_process", stats->perf_process_us_total, stats->perf_process_us_max,
                 stats->perf_process_calls);
    OutputAvgMax("perf: timing radio_post", stats->radio_post_us_total, stats->radio_post_us_max,
                 stats->radio_post_calls);

    OutputAvgMax("perf: timing drivers_total", stats->drivers_total_us, stats->drivers_total_us_max,
                 stats->drivers_total_calls);
    OutputAvgMax("perf: timing pump_thread", stats->pump_thread_us_total, stats->pump_thread_us_max,
                 stats->pump_thread_calls);
    OutputAvgMax("perf: timing send_one", stats->send_one_us_total, stats->send_one_us_max, stats->send_one_calls);

    otCliOutputFormat("perf: timing tx_done=%u tx_submit=%u\r\n", (unsigned)stats->tx_done_count,
                      (unsigned)stats->tx_submit_count);

    if (stats->send_one_calls > 0U && stats->tx_submit_count == 0U)
    {
        otCliOutputFormat("perf: timing WARN gap counters empty — linker wraps inactive?\r\n");
    }

    if (stats->tx_submit_count > 0U)
    {
        otCliOutputFormat("perf: timing gap_all avg_us=%u max_us=%u count=%u\r\n",
                          (unsigned)(stats->gap_us_total / stats->tx_submit_count), (unsigned)stats->gap_us_max,
                          (unsigned)stats->tx_submit_count);
    }
    else
    {
        otCliOutputFormat("perf: timing gap_all avg_us=- max_us=- count=0\r\n");
    }

    if (stats->gap_submit_large_count > 0U)
    {
        otCliOutputFormat("perf: timing gap_large(>%uB) avg_us=%u count=%u\r\n",
                          (unsigned)OT_PERF_TIMING_LARGE_FRAG_THRESHOLD,
                          (unsigned)(stats->gap_submit_large_us_total / stats->gap_submit_large_count),
                          (unsigned)stats->gap_submit_large_count);
    }
    else
    {
        otCliOutputFormat("perf: timing gap_large(>%uB) avg_us=- count=0\r\n",
                          (unsigned)OT_PERF_TIMING_LARGE_FRAG_THRESHOLD);
    }

    if (stats->gap_submit_small_count > 0U)
    {
        otCliOutputFormat("perf: timing gap_small(<=%uB) avg_us=%u count=%u\r\n",
                          (unsigned)OT_PERF_TIMING_LARGE_FRAG_THRESHOLD,
                          (unsigned)(stats->gap_submit_small_us_total / stats->gap_submit_small_count),
                          (unsigned)stats->gap_submit_small_count);
    }
    else
    {
        otCliOutputFormat("perf: timing gap_small(<=%uB) avg_us=- count=0\r\n",
                          (unsigned)OT_PERF_TIMING_LARGE_FRAG_THRESHOLD);
    }

    otCliOutputFormat("perf: timing gap_buckets us<500=%u 500-999=%u 1-1999=%u 2-4999=%u 5+=%u\r\n",
                      (unsigned)stats->gap_bucket_0_500us, (unsigned)stats->gap_bucket_500_1000us,
                      (unsigned)stats->gap_bucket_1_2ms, (unsigned)stats->gap_bucket_2_5ms,
                      (unsigned)stats->gap_bucket_5ms_plus);

    if (stats->perf_process_calls > 0U && stats->send_one_calls > 0U)
    {
        uint32_t perfAvg  = stats->perf_process_us_total / stats->perf_process_calls;
        uint32_t sendAvg  = stats->send_one_us_total / stats->send_one_calls;
        uint32_t wrapOver = (perfAvg > sendAvg) ? (perfAvg - sendAvg) : 0U;

        otCliOutputFormat("perf: timing pump_wrap_overhead avg_us=%u (perf_process - send_one)\r\n",
                          (unsigned)wrapOver);
    }

    if (stats->gap_submit_large_count > 0U && stats->gap_submit_small_count > 0U)
    {
        uint32_t largeAvg = stats->gap_submit_large_us_total / stats->gap_submit_large_count;
        uint32_t smallAvg = stats->gap_submit_small_us_total / stats->gap_submit_small_count;

        otCliOutputFormat("perf: timing gap_large_minus_small avg_us=%u\r\n",
                          (unsigned)((largeAvg > smallAvg) ? (largeAvg - smallAvg) : 0U));
    }
}
