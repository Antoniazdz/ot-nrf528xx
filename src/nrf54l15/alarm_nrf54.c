/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *   OpenThread alarm platform implementation for nRF54L15 (GRTC-based).
 *
 *   OT millisecond/microsecond alarms only (RCP). 802.15.4 SL lptimer lives in
 *   third_party/nrf54/platform/.
 */

 #include <openthread-core-config.h>
 #include <openthread/config.h>
 
 #include <assert.h>
 #include <stdbool.h>
 #include <stdint.h>
 #include <string.h>
 
 #include <openthread/instance.h>
 #include <openthread/platform/alarm-micro.h>
 #include <openthread/platform/alarm-milli.h>
 #include <openthread/platform/diag.h>
 #include <openthread/platform/time.h>
 
 #include <nrfx_grtc.h>
 
 #include "nrf_802154_clock.h"
 #include "openthread-system.h"
 #include "platform-config.h"
 #include "platform-nrf5.h"
 
 #include "nrf54_debug_stats.h"
 // clang-format off
 #define US_PER_MS                       1000ULL
 
 #define MIN_LPTICK_COMPARE_EVENT_TICKS  2
 #define EPOCH_32BIT_US               (1ULL << 32)
 #define EPOCH_FROM_TIME(time)        ((time) & ((uint64_t)UINT32_MAX << 32))
 // clang-format on
 
 typedef enum
 {
     kMsTimer,
     kUsTimer,
     kNumTimers
 } AlarmIndex;
 
 typedef struct
 {
     volatile bool mFireAlarm;
     uint64_t      mTargetTime;
 } AlarmData;
 
 static volatile bool sEventPending;
 static bool          sSyscounterStarted;
 static AlarmData     sTimerData[kNumTimers];
 static uint8_t       sMainCcChannel;
 static uint8_t       sMsChannel;
 static uint8_t       sUsChannel;
 static nrfx_grtc_channel_t sMsChannelData;
 static nrfx_grtc_channel_t sUsChannelData;
 
 static inline uint64_t TimeToTicks(uint64_t aTime, AlarmIndex aIndex)
 {
     if (aIndex == kMsTimer)
     {
         aTime *= US_PER_MS;
     }
 
     return aTime * OT_GRTC_US_PER_TICK;
 }
 
 static inline uint64_t TicksToTime(uint64_t aTicks, AlarmIndex aIndex)
 {
     uint64_t result = aTicks * OT_GRTC_US_PER_TICK;
 
     if (aIndex == kMsTimer)
     {
         result /= US_PER_MS;
     }
 
     return result;
 }
 
 static inline bool AlarmShallStrike(uint64_t aNow, AlarmIndex aIndex)
 {
     return aNow >= sTimerData[aIndex].mTargetTime;
 }
 
 static uint64_t GetCurrentTime(AlarmIndex aIndex);
 static void     HandleCompareMatch(AlarmIndex aIndex, bool aSkipCheck);
 
 static inline uint8_t ChannelForIndex(AlarmIndex aIndex)
 {
     return (aIndex == kMsTimer) ? sMsChannel : sUsChannel;
 }
 
 static inline nrfx_grtc_channel_t *ChannelDataForIndex(AlarmIndex aIndex)
 {
     return (aIndex == kMsTimer) ? &sMsChannelData : &sUsChannelData;
 }
 
 static void OtTimerCompareHandler(int32_t aChannel, uint64_t aCounter, void *aContext)
 {
     OT_UNUSED_VARIABLE(aChannel);
     OT_UNUSED_VARIABLE(aCounter);
 
     HandleCompareMatch((AlarmIndex)(uintptr_t)aContext, false);
 }
 
 static void GrtcSyscounterEnsureStarted(void)
 {
     int err;
 
     if (sSyscounterStarted)
     {
         return;
     }
 
     assert(nrfx_grtc_init_check());
 
     err = nrfx_grtc_syscounter_start(true, &sMainCcChannel);
     assert(err == 0);
 
     sSyscounterStarted = true;
 }
 
 static void OtGrtcChannelsInit(void)
 {
    int err;

    assert(nrfx_grtc_init_check());

    err = nrfx_grtc_channel_alloc(&sMsChannel);
    assert(err == 0);


    err = nrfx_grtc_channel_alloc(&sUsChannel);
    assert(err == 0);
 
     sMsChannelData.channel   = sMsChannel;
     sMsChannelData.handler   = OtTimerCompareHandler;
     sMsChannelData.p_context = (void *)(uintptr_t)kMsTimer;
 
     sUsChannelData.channel   = sUsChannel;
     sUsChannelData.handler   = OtTimerCompareHandler;
     sUsChannelData.p_context = (void *)(uintptr_t)kUsTimer;
 }
 
 static uint64_t ConvertT0AndDtTo64BitTime(uint32_t aT0, uint32_t aDt, const uint64_t *aNow)
 {
     uint64_t now = *aNow;
 
     if (((uint32_t)now < aT0) && ((aT0 - (uint32_t)now) > (UINT32_MAX / 2)))
     {
         now -= EPOCH_32BIT_US;
     }
     else if (((uint32_t)now > aT0) && (((uint32_t)now) - aT0 > (UINT32_MAX / 2)))
     {
         now += EPOCH_32BIT_US;
     }
 
     return (EPOCH_FROM_TIME(now)) + aT0 + aDt;
 }
 
 static uint64_t RoundUpTimeToTimerTicksMultiply(uint64_t aTime, AlarmIndex aIndex)
 {
     uint64_t ticks  = TimeToTicks(aTime, aIndex);
     uint64_t result = TicksToTime(ticks, aIndex);
 
     return result;
 }
 
 static void HandleCompareMatch(AlarmIndex aIndex, bool aSkipCheck)
 {
     (void)nrfx_grtc_syscounter_cc_disable(ChannelForIndex(aIndex));
 
     uint64_t now = GetCurrentTime(aIndex);
 
    if (aSkipCheck || AlarmShallStrike(now, aIndex))
    {
#ifdef NRF54_DEBUG_STATS
        if (aIndex == kUsTimer)
        {
            g_nrf54_debug_stats.ot_us_alarm_compare_match++;
        }
#endif
        sTimerData[aIndex].mFireAlarm = true;
        sEventPending                 = true;
        otSysEventSignalPending();
    }
 }
 
 static void TimerStartAt(uint32_t aT0, uint32_t aDt, AlarmIndex aIndex, const uint64_t *aNow)
 {
     uint64_t targetTime;
     uint64_t targetTicks;
 
     targetTime = ConvertT0AndDtTo64BitTime(aT0, aDt, aNow);
 
     sTimerData[aIndex].mTargetTime = RoundUpTimeToTimerTicksMultiply(targetTime, aIndex);
     targetTicks                    = TimeToTicks(sTimerData[aIndex].mTargetTime, aIndex);
 
     (void)nrfx_grtc_syscounter_cc_int_disable(ChannelForIndex(aIndex));
     (void)nrfx_grtc_syscounter_cc_absolute_set(ChannelDataForIndex(aIndex), targetTicks, false);
 }
 
 static void AlarmStartAt(uint32_t aT0, uint32_t aDt, AlarmIndex aIndex)
 {
     uint64_t now;
     uint64_t now_ticks;
     uint64_t now_rtc_protected;
 
     now = GetCurrentTime(aIndex);
 
     TimerStartAt(aT0, aDt, aIndex, &now);
 
     now_ticks = nrfx_grtc_syscounter_get();
     now_rtc_protected = TicksToTime(now_ticks + MIN_LPTICK_COMPARE_EVENT_TICKS, aIndex);
 
     if (AlarmShallStrike(now_rtc_protected, aIndex))
     {
         HandleCompareMatch(aIndex, true);
 
         /**
          * Normally ISR sets event flag automatically.
          * Here we are calling HandleCompareMatch explicitly and no ISR will be fired.
          * To prevent possible permanent sleep on next WFE we have to set event flag.
          */
         __SEV();
     }
     else
     {
        nrfy_grtc_sys_counter_compare_event_enable(NRF_GRTC, ChannelForIndex(aIndex));
        (void)nrfx_grtc_syscounter_cc_int_enable(ChannelForIndex(aIndex));
     }
 }
 
 static void AlarmStop(AlarmIndex aIndex)
 {
     (void)nrfx_grtc_syscounter_cc_disable(ChannelForIndex(aIndex));
 
     sTimerData[aIndex].mFireAlarm = false;
 }
 
 static uint64_t GetCurrentTime(AlarmIndex aIndex)
 {
     if (!nrfx_grtc_init_check() || !nrfx_grtc_ready_check())
     {
         return 0;
     }
 
     return TicksToTime(nrfx_grtc_syscounter_get(), aIndex);
 }
 
 void GRTC_2_IRQHandler(void)
 {  g_nrf54_debug_stats.grtc2_isr_enter++;
     nrfx_grtc_irq_handler();
 }
 void GRTC_0_IRQHandler(void)
 {
     nrfx_grtc_irq_handler();
 }
 void GRTC_1_IRQHandler(void)
 {
     nrfx_grtc_irq_handler();
 }
 void GRTC_3_IRQHandler(void)
 {
     nrfx_grtc_irq_handler();
 }

 void nrf5AlarmInit(void)
 {
     memset(sTimerData, 0, sizeof(sTimerData));
     sEventPending = false;
 
     nrf_802154_clock_init();
     nrf_802154_clock_lfclk_start();
 
    while (!nrf_802154_clock_lfclk_is_running())
    {
    }

#if OT_HFCLK_ALWAYS_ON
    nrf_802154_clock_hfclk_start();

    while (!nrf_802154_clock_hfclk_is_running())
    {
    }
#endif

    if (!nrfx_grtc_init_check())
     {
        int err;

        err = nrfx_grtc_init(OT_GRTC_IRQ_PRIORITY);
        assert(err == 0);

     }

     GrtcSyscounterEnsureStarted();
     #if OT_GRTC_ALWAYS_ON
    nrfx_grtc_active_request_set(true);
    #endif
     OtGrtcChannelsInit();
 }
 
 void nrf5AlarmDeinit(void)
 {
     AlarmStop(kMsTimer);
     AlarmStop(kUsTimer);
     sEventPending = false;
 }
 
/* CSL-F4.1-BEGIN: pending check for early alarm in main loop */
bool nrf5AlarmIsPending(void)
{
    return sEventPending || sTimerData[kUsTimer].mFireAlarm || sTimerData[kMsTimer].mFireAlarm;
}
/* CSL-F4.1-END */

 void nrf5AlarmProcess(otInstance *aInstance)
 {
     do
     {
         sEventPending = false;
 
        if (sTimerData[kUsTimer].mFireAlarm)
        {
            sTimerData[kUsTimer].mFireAlarm = false;

#ifdef NRF54_DEBUG_STATS
            g_nrf54_debug_stats.ot_us_alarm_fired++;
#endif
            otPlatAlarmMicroFired(aInstance);
        }
 
         if (sTimerData[kMsTimer].mFireAlarm)
         {
             sTimerData[kMsTimer].mFireAlarm = false;
 
 #if OPENTHREAD_CONFIG_DIAG_ENABLE
             if (otPlatDiagModeGet())
             {
                 otPlatDiagAlarmFired(aInstance);
             }
             else
 #endif
             {
                 otPlatAlarmMilliFired(aInstance);
             }
         }
 
     } while (sEventPending);
 }
 
 uint64_t nrf5AlarmGetCurrentTime(void)
 {
     return GetCurrentTime(kUsTimer);
 }
 
 uint64_t nrf5AlarmGetRawCounter(void)
 {
     if (!nrfx_grtc_init_check() || !nrfx_grtc_ready_check())
     {
         return 0;
     }
 
     return nrfx_grtc_syscounter_get();
 }
 
 uint32_t otPlatAlarmMilliGetNow(void)
 {
     return (uint32_t)(nrf5AlarmGetCurrentTime() / US_PER_MS);
 }
 
 void otPlatAlarmMilliStartAt(otInstance *aInstance, uint32_t aT0, uint32_t aDt)
 {
     OT_UNUSED_VARIABLE(aInstance);
 
     AlarmStartAt(aT0, aDt, kMsTimer);
 }
 
 void otPlatAlarmMilliStop(otInstance *aInstance)
 {
     OT_UNUSED_VARIABLE(aInstance);
 
     AlarmStop(kMsTimer);
 }
 
 uint32_t otPlatAlarmMicroGetNow(void)
 {
     return (uint32_t)nrf5AlarmGetCurrentTime();
 }
 
 void otPlatAlarmMicroStartAt(otInstance *aInstance, uint32_t aT0, uint32_t aDt)
 {
     OT_UNUSED_VARIABLE(aInstance);
 
     AlarmStartAt(aT0, aDt, kUsTimer);
 }
 
 void otPlatAlarmMicroStop(otInstance *aInstance)
 {
     OT_UNUSED_VARIABLE(aInstance);
 
     AlarmStop(kUsTimer);
 }
 
 uint64_t otPlatTimeGet(void)
 {
     return nrf5AlarmGetCurrentTime();
 }
 
 uint16_t otPlatTimeGetXtalAccuracy(void)
 {
     return OT_XTAL_ACCURACY;
 }