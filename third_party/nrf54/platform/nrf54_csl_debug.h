/*
 * Copyright (c) 2026, The OpenThread Authors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * nRF54 CSL phase-verification hooks (SubMac + platform). Stats only — no logic changes.
 */

#ifndef NRF54_CSL_DEBUG_H_
#define NRF54_CSL_DEBUG_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Peer short address (Thread RLOC16), same width as otShortAddress. */
typedef uint16_t nrf54CslPeerShort_t;

#ifdef NRF54_DEBUG_STATS

/* SubMac (sub_mac_csl_receiver.cpp). aSubMacState: SubMac::State as uint8_t. */
void nrf54CslDebugHandleCslTimer(void);
void nrf54CslDebugHandleCslReceiveAtEnter(uint8_t aSubMacState, uint32_t aWinStart, uint32_t aWinDuration);
void nrf54CslDebugReceiveAtOtCalled(uint8_t aSubMacState);
void nrf54CslDebugSkipSubMacDisabled(void);
void nrf54CslDebugSkipSubMacReceive(void);
void nrf54CslDebugSetCslParams(uint32_t aSampleTimeRadio, uint16_t aPeriodTenSymbols);
void nrf54CslDebugSyncFromRx(uint32_t aSyncTimestampUs, bool aSecEnhAck);
void nrf54CslDebugSyncFromRxNoEnhAck(void);
void nrf54CslDebugSyncFromTxAck(void);

/* Platform (radio_nrf54.c). */
void nrf54CslDebugPlatReceiveAt(uint8_t aChannel, uint32_t aWinStart, uint32_t aWinDuration, uint16_t aPeriodTenSymbols);
void nrf54CslDebugPlatDrxTimeout(void);
void nrf54CslDebugSetCslPeerShort(nrf54CslPeerShort_t aPeerShort);
void nrf54CslDebugParentRxFromPsdu(const uint8_t *aPsdu, uint8_t aLength, uint8_t aChannel, uint32_t aRxTimestampUs,
                                   bool aSecEnhAck);

#else

static inline void nrf54CslDebugHandleCslTimer(void) {}
static inline void nrf54CslDebugHandleCslReceiveAtEnter(uint8_t aSubMacState, uint32_t aWinStart, uint32_t aWinDuration)
{
    (void)aSubMacState;
    (void)aWinStart;
    (void)aWinDuration;
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

#endif /* NRF54_DEBUG_STATS */

#ifdef __cplusplus
}
#endif

#endif /* NRF54_CSL_DEBUG_H_ */
