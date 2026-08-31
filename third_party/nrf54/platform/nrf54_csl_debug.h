/*
 * Copyright (c) 2026, The OpenThread Authors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * nRF54 CSL phase-verification hooks (SubMac + platform + driver). Stats only.
 */

#ifndef NRF54_CSL_DEBUG_H_
#define NRF54_CSL_DEBUG_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t nrf54CslPeerShort_t;

#ifdef NRF54_DEBUG_STATS

uint32_t nrf54CslDebugGrtcNowUs(void);

/* SubMac (sub_mac_csl_receiver.cpp). */
void nrf54CslDebugHandleCslTimer(void);
void nrf54CslDebugHandleCslReceiveAtEnter(uint8_t aSubMacState, uint32_t aSampleTimeBefore, uint32_t aWinStart,
                                          uint32_t aWinDuration, uint32_t aTimeAhead, uint32_t aTimeAfter,
                                          uint32_t aNextTimerFire, uint32_t aLastSync, uint32_t aSampleTimeAfter);
void nrf54CslDebugReceiveAtOtCalled(uint8_t aSubMacState);
void nrf54CslDebugSkipSubMacDisabled(void);
void nrf54CslDebugSkipSubMacReceive(void);
void nrf54CslDebugSetCslParams(uint32_t aSampleTimeRadio, uint16_t aPeriodTenSymbols);
void nrf54CslDebugSyncFromRx(uint32_t aSyncTimestampUs, bool aSecEnhAck);
void nrf54CslDebugSyncFromRxNoEnhAck(void);
void nrf54CslDebugSyncFromTxAck(void);
void nrf54CslDebugRestartAfterSync(uint32_t aRewoundSampleTime);

/* Platform (radio_nrf54.c). */
void nrf54CslDebugUpdateCslSampleTime(uint32_t aAnchorUnwrappedLo, uint16_t aPeriodTenSymbols);
void nrf54CslDebugPlatReceiveAt(uint8_t aChannel, uint32_t aWinStart, uint32_t aWinDuration, uint16_t aPeriodTenSymbols);
void nrf54CslDebugPlatDrxTimeout(void);
void nrf54CslDebugSetCslPeerShort(nrf54CslPeerShort_t aPeerShort);
void nrf54CslDebugParentRxFromPsdu(const uint8_t *aPsdu, uint8_t aLength, uint8_t aChannel, uint32_t aRxTimestampUs,
                                   bool aSecEnhAck);

/* Driver IE writer (nrf_802154_ie_writer.c). */
void nrf54CslDebugIeAnchorSet(uint64_t aAnchorTime, uint16_t aPeriodTenSymbols);
void nrf54CslDebugIePeriodSet(uint16_t aPeriodTenSymbols);
void nrf54CslDebugIePhaseCalc(uint64_t aAnchorTime, uint64_t aSlTimerUs, uint64_t aRefTimeUs, uint32_t aUsToNext,
                              uint32_t aPhaseTenSymbols, uint16_t aPeriodTenSymbols, bool aAnchorSet, bool aOk,
                              bool aFallback);

/* Driver delayed_trx (nrf_802154_delayed_trx.c). */
void nrf54CslDebugDrxReceiveScheduled(uint64_t aRxTimeArg, uint64_t aTriggerTime, uint32_t aTimeoutLength,
                                      bool aOk);
void nrf54CslDebugWinCapPlan(void);
void nrf54CslDebugWinCapClose(void);

#else

static inline uint32_t nrf54CslDebugGrtcNowUs(void) { return 0U; }

static inline void nrf54CslDebugHandleCslTimer(void) {}
static inline void nrf54CslDebugHandleCslReceiveAtEnter(uint8_t aSubMacState, uint32_t aSampleTimeBefore,
                                                        uint32_t aWinStart, uint32_t aWinDuration, uint32_t aTimeAhead,
                                                        uint32_t aTimeAfter, uint32_t aNextTimerFire,
                                                        uint32_t aLastSync, uint32_t aSampleTimeAfter)
{
    (void)aSubMacState;
    (void)aSampleTimeBefore;
    (void)aWinStart;
    (void)aWinDuration;
    (void)aTimeAhead;
    (void)aTimeAfter;
    (void)aNextTimerFire;
    (void)aLastSync;
    (void)aSampleTimeAfter;
}
static inline void nrf54CslDebugReceiveAtOtCalled(uint8_t aSubMacState) { (void)aSubMacState; }
static inline void nrf54CslDebugSkipSubMacDisabled(void) {}
static inline void nrf54CslDebugSkipSubMacReceive(void) {}
static inline void nrf54CslDebugSetCslParams(uint32_t aSampleTimeRadio, uint16_t aPeriodTenSymbols)
{
    (void)aSampleTimeRadio;
    (void)aPeriodTenSymbols;
}
static inline void nrf54CslDebugSyncFromRx(uint32_t aSyncTimestampUs, bool aSecEnhAck)
{
    (void)aSyncTimestampUs;
    (void)aSecEnhAck;
}
static inline void nrf54CslDebugSyncFromRxNoEnhAck(void) {}
static inline void nrf54CslDebugSyncFromTxAck(void) {}
static inline void nrf54CslDebugRestartAfterSync(uint32_t aRewoundSampleTime) { (void)aRewoundSampleTime; }
static inline void nrf54CslDebugUpdateCslSampleTime(uint32_t aAnchorUnwrappedLo, uint16_t aPeriodTenSymbols)
{
    (void)aAnchorUnwrappedLo;
    (void)aPeriodTenSymbols;
}
static inline void nrf54CslDebugPlatReceiveAt(uint8_t aChannel, uint32_t aWinStart, uint32_t aWinDuration,
                                              uint16_t aPeriodTenSymbols)
{
    (void)aChannel;
    (void)aWinStart;
    (void)aWinDuration;
    (void)aPeriodTenSymbols;
}
static inline void nrf54CslDebugPlatDrxTimeout(void) {}
static inline void nrf54CslDebugSetCslPeerShort(nrf54CslPeerShort_t aPeerShort) { (void)aPeerShort; }
static inline void nrf54CslDebugParentRxFromPsdu(const uint8_t *aPsdu, uint8_t aLength, uint8_t aChannel,
                                                 uint32_t aRxTimestampUs, bool aSecEnhAck)
{
    (void)aPsdu;
    (void)aLength;
    (void)aChannel;
    (void)aRxTimestampUs;
    (void)aSecEnhAck;
}
static inline void nrf54CslDebugIeAnchorSet(uint64_t aAnchorTime, uint16_t aPeriodTenSymbols)
{
    (void)aAnchorTime;
    (void)aPeriodTenSymbols;
}
static inline void nrf54CslDebugIePeriodSet(uint16_t aPeriodTenSymbols) { (void)aPeriodTenSymbols; }
static inline void nrf54CslDebugIePhaseCalc(uint64_t aAnchorTime, uint64_t aSlTimerUs, uint64_t aRefTimeUs,
                                            uint32_t aUsToNext, uint32_t aPhaseTenSymbols, uint16_t aPeriodTenSymbols,
                                            bool aAnchorSet, bool aOk, bool aFallback)
{
    (void)aAnchorTime;
    (void)aSlTimerUs;
    (void)aRefTimeUs;
    (void)aUsToNext;
    (void)aPhaseTenSymbols;
    (void)aPeriodTenSymbols;
    (void)aAnchorSet;
    (void)aOk;
    (void)aFallback;
}
static inline void nrf54CslDebugDrxReceiveScheduled(uint64_t aRxTimeArg, uint64_t aTriggerTime, uint32_t aTimeoutLength,
                                                    bool aOk)
{
    (void)aRxTimeArg;
    (void)aTriggerTime;
    (void)aTimeoutLength;
    (void)aOk;
}
static inline void nrf54CslDebugWinCapPlan(void) {}
static inline void nrf54CslDebugWinCapClose(void) {}

#endif /* NRF54_DEBUG_STATS */

#ifdef __cplusplus
}
#endif

#endif /* NRF54_CSL_DEBUG_H_ */
