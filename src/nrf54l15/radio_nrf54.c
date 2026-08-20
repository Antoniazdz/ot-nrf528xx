/*
 *  Copyright (c) 2019, The OpenThread Authors.
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
 *   This file implements the OpenThread platform abstraction for radio communication.
 *
 */

#include <openthread-core-config.h>
#include <openthread/config.h>

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <openthread/link.h>

#include "utils/code_utils.h"
#include "utils/link_metrics.h"
#include "utils/mac_frame.h"

#include <platform-config.h>
#include <openthread/platform/alarm-micro.h>
#include <openthread/platform/diag.h>
#include <openthread/platform/radio.h>
#include <openthread/platform/time.h>

#include "openthread-system.h"
#include "platform-fem.h"
#include "platform-nrf5.h"

#include <hal/nrf_ficr.h>
#include <nrfx_glue.h>

#include <nrf.h>
#include <nrf_802154.h>
#include <nrf_802154_pib.h>
#include "nrf_802154_core.h"
#include "platform/nrf_802154_clock.h"

#include "nrf54_debug_stats.h"

#include <openthread-core-config.h>
#include <openthread/config.h>
#include <openthread/random_noncrypto.h>

// clang-format off

#define SHORT_ADDRESS_SIZE    2            ///< Size of MAC short address.
#define US_PER_MS             1000ULL      ///< Microseconds in millisecond.

#define RSSI_SETTLE_TIME_US   40           ///< RSSI settle time in microseconds.
#define DRX_SLOT_RX           0            ///< Delayed reception window ID for CSL.

#define CSL_UNCERT            20           ///< The Uncertainty of the scheduling CSL of transmission by the parent, in ±10 us units.

#if defined(__ICCARM__)
_Pragma("diag_suppress=Pe167")
#endif

enum
{
    NRF54L15_RECEIVE_SENSITIVITY  = -102, // dBm (POC placeholder; tune for nRF54L15)
    NRF54L15_MIN_CCA_ED_THRESHOLD = -92,  // dBm (POC placeholder; tune for nRF54L15)
};

// clang-format on

static bool sDisabled;

static nrf_802154_state_t sDriverState;

static otError      sReceiveError = OT_ERROR_NONE;
static otRadioFrame sReceivedFrames[NRF_802154_RX_BUFFERS];
static otRadioFrame sTransmitFrame;
static uint8_t      sTransmitPsdu[OT_RADIO_FRAME_MAX_SIZE + 1];

#if OPENTHREAD_CONFIG_MAC_HEADER_IE_SUPPORT
static otExtAddress  sExtAddress;
static otRadioIeInfo sTransmitIeInfo;
static otInstance   *sInstance = NULL;
#endif

static otRadioFrame sAckFrame;
static bool         sAckedWithFramePending;

static int8_t   sMaxTxPowerTable[OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MAX - OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MIN + 1];
static int8_t   sDefaultTxPower;
static int8_t   sLnaGain    = 0;
static uint16_t sRegionCode = 0;

static uint32_t sEnergyDetectionTime;
static uint8_t  sEnergyDetectionChannel;
static int8_t   sEnergyDetected;

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
static uint32_t      sCslPeriod;
static uint32_t      sCslSampleTime;
static const uint8_t sCslIeHeader[OT_IE_HEADER_SIZE] = {CSL_IE_HEADER_BYTES_LO, CSL_IE_HEADER_BYTES_HI};

static uint16_t getCslPhase(void);
#endif // OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE

typedef enum
{
    kPendingEventSleep,                // Requested to enter Sleep state.
    kPendingEventFrameTransmitted,     // Transmitted frame and received ACK (if requested).
    kPendingEventChannelAccessFailure, // Failed to transmit frame (channel busy).
    kPendingEventInvalidOrNoAck,       // Failed to transmit frame (received invalid or no ACK).
    kPendingEventReceiveFailed,        // Failed to receive a valid frame.
    kPendingEventEnergyDetectionStart, // Requested to start Energy Detection procedure.
    kPendingEventEnergyDetected,       // Energy Detection finished.
} RadioPendingEvents;

static uint32_t sPendingEvents;

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
static uint32_t         sMacFrameCounter;
static uint32_t         sPrevMacFrameCounter;
static uint8_t          sKeyId;
static otMacKeyMaterial sPrevKey;
static otMacKeyMaterial sCurrKey;
static otMacKeyMaterial sNextKey;
static bool             sAckedWithSecEnhAck;
static uint32_t         sAckFrameCounter;
static uint8_t          sAckKeyId;
#endif

static inline void SetRadioDriverState(nrf_802154_state_t aState)
{
    sDriverState = aState;
}

static inline bool IsRadioDriverStateSleep(void)
{
    return sDriverState == NRF_802154_STATE_SLEEP;
}

static otRadioState MapRadioDriverStateToOt(nrf_802154_state_t aState)
{
    switch (aState)
    {
    case NRF_802154_STATE_SLEEP:
        return OT_RADIO_STATE_SLEEP;

    case NRF_802154_STATE_RECEIVE:
    case NRF_802154_STATE_ENERGY_DETECTION:
        return OT_RADIO_STATE_RECEIVE;

    case NRF_802154_STATE_TRANSMIT:
    case NRF_802154_STATE_CCA:
    case NRF_802154_STATE_CONTINUOUS_CARRIER:
        return OT_RADIO_STATE_TRANSMIT;

    default:
        assert(false);
        return OT_RADIO_STATE_RECEIVE;
    }
}

static int8_t GetTransmitPowerForChannel(uint8_t aChannel)
{
    int8_t channelMaxPower = nrf5GetChannelMaxTransmitPower(aChannel);
    int8_t power           = 0; // 0 dbm as default value

    if (sDefaultTxPower != OT_RADIO_POWER_INVALID)
    {
        power = (channelMaxPower < sDefaultTxPower) ? channelMaxPower : sDefaultTxPower;
    }
    else if (channelMaxPower != OT_RADIO_POWER_INVALID)
    {
        power = channelMaxPower;
    }

    return power;
}

static uint64_t GetRxTimestamp(uint64_t aTime, uint8_t aLength)
{
    OT_UNUSED_VARIABLE(aLength);

    if (aTime == NRF_802154_NO_TIMESTAMP)
    {
        g_nrf54_debug_stats.rx_no_timestamp++;
        return nrf5AlarmGetCurrentTime();
    }

    g_nrf54_debug_stats.rx_timestamp_ok++;
    return aTime;
}

static void dataInit(void)
{
    sDisabled = true;

    sDefaultTxPower      = OT_RADIO_POWER_INVALID;
    sTransmitFrame.mPsdu = sTransmitPsdu + 1;
#if OPENTHREAD_CONFIG_MAC_HEADER_IE_SUPPORT
    sTransmitFrame.mInfo.mTxInfo.mIeInfo = &sTransmitIeInfo;
#endif

    sReceiveError = OT_ERROR_NONE;

    for (uint32_t i = 0; i < NRF_802154_RX_BUFFERS; i++)
    {
        sReceivedFrames[i].mPsdu = NULL;
    }

    for (size_t i = 0; i < otARRAY_LENGTH(sMaxTxPowerTable); i++)
    {
        sMaxTxPowerTable[i] = OT_RADIO_POWER_INVALID;
    }

    memset(&sAckFrame, 0, sizeof(sAckFrame));

    sPrevMacFrameCounter = 0;

    SetRadioDriverState(NRF_802154_STATE_SLEEP);
}

static void convertShortAddress(uint8_t *aTo, uint16_t aFrom)
{
    aTo[0] = (uint8_t)aFrom;
    aTo[1] = (uint8_t)(aFrom >> 8);
}

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE || OPENTHREAD_CONFIG_MLE_LINK_METRICS_SUBJECT_ENABLE
static void convertExtAddress(uint8_t *aTo, const otExtAddress *aFrom)
{
    for (uint8_t i = 0; i < sizeof(otExtAddress); i++)
    {
        aTo[i] = aFrom->m8[sizeof(otExtAddress) - i - 1];
    }
}
#endif

static inline bool isPendingEventSet(RadioPendingEvents aEvent)
{
    return sPendingEvents & (1UL << aEvent);
}

static void setPendingEvent(RadioPendingEvents aEvent)
{
    volatile uint32_t pendingEvents;
    uint32_t          bitToSet = 1UL << aEvent;

    do
    {
        pendingEvents = __LDREXW((uint32_t *)&sPendingEvents);
        pendingEvents |= bitToSet;
    } while (__STREXW(pendingEvents, (uint32_t *)&sPendingEvents));

    otSysEventSignalPending();
}

static void resetPendingEvent(RadioPendingEvents aEvent)
{
    volatile uint32_t pendingEvents;
    uint32_t          bitsToRemain = ~(1UL << aEvent);

    do
    {
        pendingEvents = __LDREXW((uint32_t *)&sPendingEvents);
        pendingEvents &= bitsToRemain;
    } while (__STREXW(pendingEvents, (uint32_t *)&sPendingEvents));
}

static inline void clearPendingEvents(void)
{
    // Clear pending events that could cause race in the MAC layer.
    volatile uint32_t pendingEvents;
    uint32_t          bitsToRemain = ~(0UL);

    bitsToRemain &= ~(1UL << kPendingEventSleep);

    do
    {
        pendingEvents = __LDREXW((uint32_t *)&sPendingEvents);
        pendingEvents &= bitsToRemain;
    } while (__STREXW(pendingEvents, (uint32_t *)&sPendingEvents));
}

#define NRF54_DEBUG_PING_TX_LEN_MIN 60U
#define NRF54_DEBUG_PING_TX_LEN_MAX 120U

static bool nrf54DebugFrameAckRequested(const otRadioFrame *aFrame)
{
    uint16_t frameControl;

    if (aFrame->mLength < 2)
    {
        return false;
    }

    frameControl = (uint16_t)aFrame->mPsdu[0] | ((uint16_t)aFrame->mPsdu[1] << 8);

    return (frameControl & 0x0020U) != 0U;
}

static bool nrf54DebugIsPingSizedTx(const otRadioFrame *aFrame)
{
    uint16_t frameControl;

    if (aFrame->mLength < 2)
    {
        return false;
    }

    frameControl = (uint16_t)aFrame->mPsdu[0] | ((uint16_t)aFrame->mPsdu[1] << 8);

    /* 802.15.4 data frame, ACK req, typical ICMP-over-Thread length (excludes short MLE/broadcast). */
    return ((frameControl & 0x0007U) == 0x0001U) && ((frameControl & 0x0020U) != 0U) &&
           aFrame->mLength >= NRF54_DEBUG_PING_TX_LEN_MIN && aFrame->mLength <= NRF54_DEBUG_PING_TX_LEN_MAX;
}

static void nrf54DebugRecordTxImmediateFail(radio_state_t         aDriverStateBefore,
                                            nrf_802154_tx_error_t aTxError,
                                            const otRadioFrame   *aFrame)
{
    g_nrf54_debug_stats.last_fail_driver_state    = (uint32_t)aDriverStateBefore;
    g_nrf54_debug_stats.last_fail_immediate_error = aTxError;
    g_nrf54_debug_stats.last_fail_tx_length       = aFrame->mLength;
    g_nrf54_debug_stats.last_fail_tx_channel      = aFrame->mChannel;
    g_nrf54_debug_stats.last_fail_ack_requested   = nrf54DebugFrameAckRequested(aFrame) ? 1U : 0U;

    switch (aDriverStateBefore)
    {
    case RADIO_STATE_SLEEP:
        g_nrf54_debug_stats.tx_fail_state_sleep++;
        break;

    case RADIO_STATE_RX:
        g_nrf54_debug_stats.tx_fail_state_rx++;
        break;

    case RADIO_STATE_TX_ACK:
        g_nrf54_debug_stats.tx_fail_state_tx_ack++;
        break;

    case RADIO_STATE_CCA_TX:
        g_nrf54_debug_stats.tx_fail_state_cca_tx++;
        break;

    case RADIO_STATE_TX:
        g_nrf54_debug_stats.tx_fail_state_tx++;
        break;

    case RADIO_STATE_RX_ACK:
        g_nrf54_debug_stats.tx_fail_state_rx_ack++;
        break;

    default:
        g_nrf54_debug_stats.tx_fail_state_other++;
        break;
    }

    switch (aTxError)
    {
    case NRF_802154_TX_ERROR_TIMESLOT_DENIED:
        g_nrf54_debug_stats.tx_raw_err_timeslot_denied++;
        break;

    case NRF_802154_TX_ERROR_INVALID_REQUEST:
        g_nrf54_debug_stats.tx_raw_err_invalid_request++;
        break;

    case NRF_802154_TX_ERROR_KEY_ID_INVALID:
        g_nrf54_debug_stats.tx_raw_err_key_id_invalid++;
        break;

    case NRF_802154_TX_ERROR_FRAME_COUNTER_ERROR:
        g_nrf54_debug_stats.tx_raw_err_frame_counter_error++;
        break;

    case NRF_802154_TX_ERROR_TIMESTAMP_ENCODING_ERROR:
        g_nrf54_debug_stats.tx_raw_err_timestamp_encoding++;
        break;

    default:
        g_nrf54_debug_stats.tx_raw_err_other++;
        break;
    }

    if (nrf54DebugIsPingSizedTx(aFrame))
    {
        g_nrf54_debug_stats.tx_fail_ping_sized++;
        g_nrf54_debug_stats.last_ping_fail_driver_state    = (uint32_t)aDriverStateBefore;
        g_nrf54_debug_stats.last_ping_fail_immediate_error = aTxError;
        g_nrf54_debug_stats.last_ping_fail_tx_length       = aFrame->mLength;
    }
}

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
static void txAckProcessSecurity(uint8_t *aAckFrame)
{
    otRadioFrame      ackFrame;
    otMacKeyMaterial *key = NULL;
    uint8_t           keyId;

    sAckedWithSecEnhAck = false;
    otEXPECT(aAckFrame[SECURITY_ENABLED_OFFSET] & SECURITY_ENABLED_BIT);

    memset(&ackFrame, 0, sizeof(ackFrame));
    ackFrame.mPsdu   = &aAckFrame[1];
    ackFrame.mLength = aAckFrame[0];

    keyId = otMacFrameGetKeyId(&ackFrame);

    otEXPECT(otMacFrameIsKeyIdMode1(&ackFrame) && keyId != 0);

    if (keyId == sKeyId)
    {
        key              = &sCurrKey;
        sAckFrameCounter = sMacFrameCounter++;
    }
    else if (keyId == sKeyId - 1)
    {
        key              = &sPrevKey;
        sAckFrameCounter = sPrevMacFrameCounter++;
    }
    else if (keyId == sKeyId + 1)
    {
        key = &sNextKey;
        // Openthread does not maintain future frame counter.
        // Mac frame counter would be overwritten after key rotation leading to
        // frames being dropped due to counter value lower than in acks.
        sAckFrameCounter = 0;
    }
    else
    {
        otEXPECT(false);
    }

    sAckKeyId           = keyId;
    sAckedWithSecEnhAck = true;

    ackFrame.mInfo.mTxInfo.mAesKey = key;

    otMacFrameSetKeyId(&ackFrame, keyId);
    otMacFrameSetFrameCounter(&ackFrame, sAckFrameCounter);

    otMacFrameProcessTransmitAesCcm(&ackFrame, &sExtAddress);

exit:
    return;
}
#endif // OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2

#if !OPENTHREAD_CONFIG_ENABLE_PLATFORM_EUI64_CUSTOM_SOURCE
void otPlatRadioGetIeeeEui64(otInstance *aInstance, uint8_t *aIeeeEui64)
{
    OT_UNUSED_VARIABLE(aInstance);

    uint64_t factoryAddress;
    uint32_t index = 0;

    // Set the MAC Address Block Larger (MA-L) formerly called OUI.
    aIeeeEui64[index++] = (OPENTHREAD_CONFIG_STACK_VENDOR_OUI >> 16) & 0xff;
    aIeeeEui64[index++] = (OPENTHREAD_CONFIG_STACK_VENDOR_OUI >> 8) & 0xff;
    aIeeeEui64[index++] = OPENTHREAD_CONFIG_STACK_VENDOR_OUI & 0xff;

    // Use device identifier assigned during the production.
    factoryAddress = (uint64_t)nrf_ficr_deviceid_get(NRF_FICR, 0) << 32;
    factoryAddress |= nrf_ficr_deviceid_get(NRF_FICR, 1);
    memcpy(aIeeeEui64 + index, &factoryAddress, sizeof(factoryAddress) - index);
}
#endif // OPENTHREAD_CONFIG_ENABLE_PLATFORM_EUI64_CUSTOM_SOURCE

void otPlatRadioSetPanId(otInstance *aInstance, uint16_t aPanId)
{
    OT_UNUSED_VARIABLE(aInstance);

    uint8_t address[SHORT_ADDRESS_SIZE];
    convertShortAddress(address, aPanId);

    nrf_802154_pan_id_set(address);
}

void otPlatRadioSetExtendedAddress(otInstance *aInstance, const otExtAddress *aExtAddress)
{
    OT_UNUSED_VARIABLE(aInstance);
#if OPENTHREAD_CONFIG_MAC_HEADER_IE_SUPPORT
    for (size_t i = 0; i < sizeof(*aExtAddress); i++)
    {
        sExtAddress.m8[i] = aExtAddress->m8[sizeof(*aExtAddress) - 1 - i];
    }
#endif
    nrf_802154_extended_address_set(aExtAddress->m8);
}

void otPlatRadioSetShortAddress(otInstance *aInstance, uint16_t aShortAddress)
{
    OT_UNUSED_VARIABLE(aInstance);

    uint8_t address[SHORT_ADDRESS_SIZE];
    convertShortAddress(address, aShortAddress);

    nrf_802154_short_address_set(address);
}

void nrf5RadioInit(void)
{
    dataInit();
#if OPENTHREAD_CONFIG_MLE_LINK_METRICS_SUBJECT_ENABLE
    otLinkMetricsInit(NRF54L15_RECEIVE_SENSITIVITY);
#endif
    nrf_802154_init();
    nrf_802154_clock_hfclk_start();
}

void nrf5RadioDeinit(void)
{
    nrf_802154_sleep();
    nrf_802154_deinit();
    sPendingEvents = 0;
    SetRadioDriverState(NRF_802154_STATE_SLEEP);
}

void nrf5RadioClearPendingEvents(void)
{
    sPendingEvents = 0;

    for (uint32_t i = 0; i < NRF_802154_RX_BUFFERS; i++)
    {
        if (sReceivedFrames[i].mPsdu != NULL)
        {
            uint8_t *bufferAddress   = &sReceivedFrames[i].mPsdu[-1];
            sReceivedFrames[i].mPsdu = NULL;
            nrf_802154_buffer_free_raw(bufferAddress);
        }
    }
}

otRadioState otPlatRadioGetState(otInstance *aInstance)
{
    OT_UNUSED_VARIABLE(aInstance);

    if (sDisabled)
    {
        return OT_RADIO_STATE_DISABLED;
    }

    return MapRadioDriverStateToOt(sDriverState);
}

bool otPlatRadioIsEnabled(otInstance *aInstance)
{
    OT_UNUSED_VARIABLE(aInstance);

    return !sDisabled;
}

otError otPlatRadioEnable(otInstance *aInstance)
{
    otError error;

#if !OPENTHREAD_CONFIG_MAC_HEADER_IE_SUPPORT
    OT_UNUSED_VARIABLE(aInstance);
#else
    sInstance = aInstance;
#endif

    if (sDisabled)
    {
        sDisabled = false;
        error     = OT_ERROR_NONE;
    }
    else
    {
        error = OT_ERROR_INVALID_STATE;
    }

    return error;
}

otError otPlatRadioDisable(otInstance *aInstance)
{
    otError error = OT_ERROR_NONE;

    otEXPECT(otPlatRadioIsEnabled(aInstance));
    otEXPECT_ACTION(otPlatRadioGetState(aInstance) == OT_RADIO_STATE_SLEEP || isPendingEventSet(kPendingEventSleep),
                    error = OT_ERROR_INVALID_STATE);

    sDisabled = true;

exit:
    return error;
}

otError otPlatRadioSleep(otInstance *aInstance)
{
    OT_UNUSED_VARIABLE(aInstance);

    if (nrf_802154_sleep_if_idle() == NRF_802154_SLEEP_ERROR_NONE)
    {
        nrf5FemDisable();
        clearPendingEvents();
        SetRadioDriverState(NRF_802154_STATE_SLEEP);
    }
    else
    {
        clearPendingEvents();
        setPendingEvent(kPendingEventSleep);
    }

    return OT_ERROR_NONE;
}

otError otPlatRadioReceive(otInstance *aInstance, uint8_t aChannel)
{
    OT_UNUSED_VARIABLE(aInstance);

    bool result;

    nrf_802154_channel_set(aChannel);
    if (IsRadioDriverStateSleep())
    {
        // Enable FEM before RADIO leaving SLEEP state.
        nrf5FemEnable();
    }

    nrf_802154_tx_power_set(GetTransmitPowerForChannel(aChannel));
    result = nrf_802154_receive();
    clearPendingEvents();

    if (result)
    {
        SetRadioDriverState(NRF_802154_STATE_RECEIVE);
    }

    return result ? OT_ERROR_NONE : OT_ERROR_INVALID_STATE;
}

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
static uint64_t unwrapFutureRadioTimeUs(uint32_t aTimeUs)
{
    uint64_t nowUs = otPlatTimeGet();
    uint32_t nowLo = (uint32_t)nowUs;
    uint64_t rxTime = (nowUs & ~(uint64_t)UINT32_MAX) | aTimeUs;

    if (nowLo > aTimeUs)
    {
        rxTime += (uint64_t)UINT32_MAX + 1U;
    }

    return rxTime;
}

otError otPlatRadioReceiveAt(otInstance *aInstance, uint8_t aChannel, uint32_t aStart, uint32_t aDuration)
{
    OT_UNUSED_VARIABLE(aInstance);

    bool     result;
    uint64_t rxTime = unwrapFutureRadioTimeUs(aStart);

    g_nrf54_debug_stats.csl_receive_at_enter++;
    g_nrf54_debug_stats.last_csl_channel              = aChannel;
    g_nrf54_debug_stats.last_csl_win_start            = aStart;
    g_nrf54_debug_stats.last_csl_win_duration         = aDuration;
    g_nrf54_debug_stats.last_csl_receive_at_arg_start = (uint32_t)rxTime;
    g_nrf54_debug_stats.last_grtc_at_csl_receive_at   = (uint32_t)otPlatTimeGet();

    nrf_802154_tx_power_set(GetTransmitPowerForChannel(aChannel));
    (void)nrf_802154_receive_at_scheduled_cancel(DRX_SLOT_RX);
    result = nrf_802154_receive_at(rxTime, aDuration, aChannel, DRX_SLOT_RX);
    clearPendingEvents();

    if (result)
    {
        g_nrf54_debug_stats.csl_receive_at_ok++;
    }
    else
    {
        g_nrf54_debug_stats.csl_receive_at_fail++;
    }

    return result ? OT_ERROR_NONE : OT_ERROR_FAILED;
}
#endif

#if OPENTHREAD_CONFIG_MAC_HEADER_IE_SUPPORT
static void nrf54ProcessTransmitSecurity(otRadioFrame *aFrame)
{
    bool processSecurity = false;

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
    if ((sCslPeriod > 0) && !aFrame->mInfo.mTxInfo.mIsARetx)
    {
        otMacFrameSetCslIe(aFrame, (uint16_t)sCslPeriod, getCslPhase());
    }
#endif

#if OPENTHREAD_CONFIG_TIME_SYNC_ENABLE
    if (aFrame->mInfo.mTxInfo.mIeInfo->mTimeIeOffset != 0)
    {
        uint8_t *timeIe = aFrame->mPsdu + aFrame->mInfo.mTxInfo.mIeInfo->mTimeIeOffset;
        uint64_t time   = otPlatTimeGet() + aFrame->mInfo.mTxInfo.mIeInfo->mNetworkTimeOffset;

        *timeIe = aFrame->mInfo.mTxInfo.mIeInfo->mTimeSyncSeq;

        *(++timeIe) = (uint8_t)(time & 0xff);
        for (uint8_t i = 1; i < sizeof(uint64_t); i++)
        {
            time        = time >> 8;
            *(++timeIe) = (uint8_t)(time & 0xff);
        }

        processSecurity = true;
    }
#endif // OPENTHREAD_CONFIG_TIME_SYNC_ENABLE

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
    otEXPECT(otMacFrameIsSecurityEnabled(aFrame) && otMacFrameIsKeyIdMode1(aFrame) &&
             !aFrame->mInfo.mTxInfo.mIsSecurityProcessed);

    aFrame->mInfo.mTxInfo.mAesKey = &sCurrKey;
    processSecurity               = true;
#endif // OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2

    otEXPECT(processSecurity);
    g_nrf54_debug_stats.tx_late_encrypt++;
    g_nrf54_debug_stats.last_tx_late_encrypted = 1U;

    if (nrf54DebugIsPingSizedTx(aFrame))
    {
        g_nrf54_debug_stats.tx_late_encrypt_ping++;
    }

    otMacFrameProcessTransmitAesCcm(aFrame, &sExtAddress);

exit:
    return;
}
#endif // OPENTHREAD_CONFIG_MAC_HEADER_IE_SUPPORT

otError otPlatRadioTransmit(otInstance *aInstance, otRadioFrame *aFrame)
{
    nrf_802154_tx_error_t txError = NRF_802154_TX_ERROR_NONE;
    otError               error   = OT_ERROR_NONE;

    g_nrf54_debug_stats.tx_enter++;
    g_nrf54_debug_stats.last_tx_length          = aFrame->mLength;
    g_nrf54_debug_stats.last_tx_channel         = aFrame->mChannel;
    g_nrf54_debug_stats.last_tx_csma            = aFrame->mInfo.mTxInfo.mCsmaCaEnabled;
    g_nrf54_debug_stats.last_tx_max_backoffs    = aFrame->mInfo.mTxInfo.mMaxCsmaBackoffs;
    g_nrf54_debug_stats.last_tx_immediate_error = NRF_802154_TX_ERROR_NONE;
    g_nrf54_debug_stats.last_ack_present        = 0;
    g_nrf54_debug_stats.last_tx_counter_injected = 0U;
    g_nrf54_debug_stats.last_tx_late_encrypted   = 0U;

    aFrame->mPsdu[-1] = aFrame->mLength;

    if (IsRadioDriverStateSleep())
    {
        // Enable FEM before RADIO leaving SLEEP state.
        nrf5FemEnable();
    }

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
    if (otMacFrameIsSecurityEnabled(aFrame) && otMacFrameIsKeyIdMode1(aFrame) && !aFrame->mInfo.mTxInfo.mIsARetx)
    {
        otMacFrameSetKeyId(aFrame, sKeyId);
        otMacFrameSetFrameCounter(aFrame, sMacFrameCounter++);
        g_nrf54_debug_stats.tx_counter_inject++;
        g_nrf54_debug_stats.last_tx_counter_injected = 1U;
    }

#if OPENTHREAD_CONFIG_MAC_HEADER_IE_SUPPORT
    /* nRF54 driver never calls nrf_802154_tx_started(); encrypt before driver TX (nRF52 model). */
    nrf54ProcessTransmitSecurity(aFrame);
#endif


    if (aFrame->mInfo.mTxInfo.mTxDelay != 0)
    {
#if NRF_802154_DELAYED_TRX_ENABLED
        nrf_802154_transmit_at_metadata_t atMetadata = {
            .frame_props         = NRF_802154_TRANSMITTED_FRAME_PROPS_DEFAULT_INIT,
            .cca                 = true,
            .channel             = aFrame->mChannel,
            .tx_power            = {.use_metadata_value = false},
            .extra_cca_attempts  = 0,
            .tx_timestamp_encode = false,
        };
        uint64_t txTime = (uint64_t)aFrame->mInfo.mTxInfo.mTxDelayBaseTime + aFrame->mInfo.mTxInfo.mTxDelay;

        txError = nrf_802154_transmit_raw_at(&aFrame->mPsdu[-1], txTime, &atMetadata);
#else
        error = OT_ERROR_NOT_IMPLEMENTED;
#endif
    }
    else
#endif
    {
        nrf_802154_channel_set(aFrame->mChannel);

#if NRF_802154_CSMA_CA_ENABLED
        if (aFrame->mInfo.mTxInfo.mCsmaCaEnabled)
        {
            nrf_802154_transmit_csma_ca_metadata_t csmaMetadata = {
                .frame_props         = NRF_802154_TRANSMITTED_FRAME_PROPS_DEFAULT_INIT,
                .tx_power            = {.use_metadata_value = false},
                .tx_channel          = {.use_metadata_value = true, .channel = aFrame->mChannel},
                .tx_timestamp_encode = false,
            };

            (void)nrf_802154_csma_ca_max_backoffs_set(aFrame->mInfo.mTxInfo.mMaxCsmaBackoffs);
            g_nrf54_debug_stats.tx_csma_enter++;
            {
                radio_state_t driverStateBefore = nrf_802154_core_state_get();

                g_nrf54_debug_stats.last_driver_state = driverStateBefore;
                txError                               = nrf_802154_transmit_csma_ca_raw(&aFrame->mPsdu[-1], &csmaMetadata);
                g_nrf54_debug_stats.last_tx_immediate_error = txError;

                if (txError != NRF_802154_TX_ERROR_NONE)
                {
                    g_nrf54_debug_stats.tx_csma_immediate_error++;
                    nrf54DebugRecordTxImmediateFail(driverStateBefore, txError, aFrame);
                }
            }
        }
        else
#endif
        {
            /* Match nRF52 radio.c: CCA runs only via the driver CSMA-CA module.
             * transmit_raw() must not honor mCsmaCaEnabled — OT sets it true for
             * data/MLE even when CSMA_CA_ENABLED=0, which caused TxErrCca storms. */
            nrf_802154_transmit_metadata_t metadata = {
                .frame_props         = NRF_802154_TRANSMITTED_FRAME_PROPS_DEFAULT_INIT,
                .cca                 = false,
                .tx_power            = {.use_metadata_value = false},
                .tx_channel          = {.use_metadata_value = false},
                .tx_timestamp_encode = false,
            };

            g_nrf54_debug_stats.tx_raw_enter++;
            {
                radio_state_t driverStateBefore = nrf_802154_core_state_get();

                g_nrf54_debug_stats.last_driver_state = driverStateBefore;
                txError                               = nrf_802154_transmit_raw(&aFrame->mPsdu[-1], &metadata);
                g_nrf54_debug_stats.last_tx_immediate_error = txError;

                if (txError != NRF_802154_TX_ERROR_NONE)
                {
                    g_nrf54_debug_stats.tx_raw_immediate_error++;
                    nrf54DebugRecordTxImmediateFail(driverStateBefore, txError, aFrame);
                }
            }
        }
    }

    if (error == OT_ERROR_NONE && txError == NRF_802154_TX_ERROR_NONE)
    {
        SetRadioDriverState(NRF_802154_STATE_TRANSMIT);
    }

    clearPendingEvents();
    otPlatRadioTxStarted(aInstance, aFrame);

    if (error == OT_ERROR_NONE && txError != NRF_802154_TX_ERROR_NONE)
    {
        setPendingEvent(kPendingEventChannelAccessFailure);
    }

    return error;
}

otRadioFrame *otPlatRadioGetTransmitBuffer(otInstance *aInstance)
{
    OT_UNUSED_VARIABLE(aInstance);

    return &sTransmitFrame;
}

int8_t otPlatRadioGetRssi(otInstance *aInstance)
{
    OT_UNUSED_VARIABLE(aInstance);

    // Ensure the RSSI measurement is done after RSSI settling time.
    // This is necessary for the Channel Monitor feature which quickly switches between channels.
    NRFX_DELAY_US(RSSI_SETTLE_TIME_US);

    nrf_802154_rssi_measure_begin();

    return nrf_802154_rssi_last_get();
}

otRadioCaps otPlatRadioGetCaps(otInstance *aInstance)
{
    OT_UNUSED_VARIABLE(aInstance);

    return (otRadioCaps)(OT_RADIO_CAPS_ENERGY_SCAN | OT_RADIO_CAPS_ACK_TIMEOUT | OT_RADIO_CAPS_CSMA_BACKOFF |
#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
                         OT_RADIO_CAPS_TRANSMIT_SEC | OT_RADIO_CAPS_TRANSMIT_TIMING | OT_RADIO_CAPS_RECEIVE_TIMING |
#endif
                         OT_RADIO_CAPS_SLEEP_TO_TX);
}

bool otPlatRadioGetPromiscuous(otInstance *aInstance)
{
    OT_UNUSED_VARIABLE(aInstance);

    return nrf_802154_promiscuous_get();
}

void otPlatRadioSetPromiscuous(otInstance *aInstance, bool aEnable)
{
    OT_UNUSED_VARIABLE(aInstance);

    nrf_802154_promiscuous_set(aEnable);
}

void otPlatRadioEnableSrcMatch(otInstance *aInstance, bool aEnable)
{
    OT_UNUSED_VARIABLE(aInstance);

    nrf_802154_auto_pending_bit_set(aEnable);
}

otError otPlatRadioAddSrcMatchShortEntry(otInstance *aInstance, uint16_t aShortAddress)
{
    OT_UNUSED_VARIABLE(aInstance);

    otError error;

    uint8_t shortAddress[SHORT_ADDRESS_SIZE];
    convertShortAddress(shortAddress, aShortAddress);

    if (nrf_802154_pending_bit_for_addr_set(shortAddress, false))
    {
        error = OT_ERROR_NONE;
    }
    else
    {
        error = OT_ERROR_NO_BUFS;
    }

    return error;
}

otError otPlatRadioAddSrcMatchExtEntry(otInstance *aInstance, const otExtAddress *aExtAddress)
{
    OT_UNUSED_VARIABLE(aInstance);

    otError error;

    if (nrf_802154_pending_bit_for_addr_set(aExtAddress->m8, true))
    {
        error = OT_ERROR_NONE;
    }
    else
    {
        error = OT_ERROR_NO_BUFS;
    }

    return error;
}

otError otPlatRadioClearSrcMatchShortEntry(otInstance *aInstance, uint16_t aShortAddress)
{
    OT_UNUSED_VARIABLE(aInstance);

    otError error;

    uint8_t shortAddress[SHORT_ADDRESS_SIZE];
    convertShortAddress(shortAddress, aShortAddress);

    if (nrf_802154_pending_bit_for_addr_clear(shortAddress, false))
    {
        error = OT_ERROR_NONE;
    }
    else
    {
        error = OT_ERROR_NO_ADDRESS;
    }

    return error;
}

otError otPlatRadioClearSrcMatchExtEntry(otInstance *aInstance, const otExtAddress *aExtAddress)
{
    OT_UNUSED_VARIABLE(aInstance);

    otError error;

    if (nrf_802154_pending_bit_for_addr_clear(aExtAddress->m8, true))
    {
        error = OT_ERROR_NONE;
    }
    else
    {
        error = OT_ERROR_NO_ADDRESS;
    }

    return error;
}

void otPlatRadioClearSrcMatchShortEntries(otInstance *aInstance)
{
    OT_UNUSED_VARIABLE(aInstance);

    nrf_802154_pending_bit_for_addr_reset(false);
}

void otPlatRadioClearSrcMatchExtEntries(otInstance *aInstance)
{
    OT_UNUSED_VARIABLE(aInstance);

    nrf_802154_pending_bit_for_addr_reset(true);
}

otError otPlatRadioEnergyScan(otInstance *aInstance, uint8_t aScanChannel, uint16_t aScanDuration)
{
    OT_UNUSED_VARIABLE(aInstance);

    sEnergyDetectionTime    = (uint32_t)aScanDuration * 1000UL;
    sEnergyDetectionChannel = aScanChannel;

    clearPendingEvents();

    nrf_802154_channel_set(aScanChannel);

    if (nrf_802154_energy_detection(sEnergyDetectionTime))
    {
        resetPendingEvent(kPendingEventEnergyDetectionStart);
        SetRadioDriverState(NRF_802154_STATE_ENERGY_DETECTION);
    }
    else
    {
        setPendingEvent(kPendingEventEnergyDetectionStart);
    }

    return OT_ERROR_NONE;
}

otError otPlatRadioGetTransmitPower(otInstance *aInstance, int8_t *aPower)
{
    OT_UNUSED_VARIABLE(aInstance);

    otError error = OT_ERROR_NONE;

    if (aPower == NULL)
    {
        error = OT_ERROR_INVALID_ARGS;
    }
    else
    {
        *aPower = nrf_802154_tx_power_get();
    }

    return error;
}

otError otPlatRadioSetTransmitPower(otInstance *aInstance, int8_t aPower)
{
    OT_UNUSED_VARIABLE(aInstance);
    uint8_t channel = nrf_802154_channel_get();
    otError error   = OT_ERROR_NONE;

    otEXPECT_ACTION(aPower != OT_RADIO_POWER_INVALID, error = OT_ERROR_INVALID_ARGS);
    sDefaultTxPower = aPower;
    nrf_802154_tx_power_set(GetTransmitPowerForChannel(channel));

exit:
    return error;
}

otError otPlatRadioGetCcaEnergyDetectThreshold(otInstance *aInstance, int8_t *aThreshold)
{
    OT_UNUSED_VARIABLE(aInstance);

    otError              error = OT_ERROR_NONE;
    nrf_802154_cca_cfg_t ccaConfig;

    if (aThreshold == NULL)
    {
        error = OT_ERROR_INVALID_ARGS;
    }
    else
    {
        nrf_802154_cca_cfg_get(&ccaConfig);
        // The radio driver has no function to convert ED threshold to dBm
        *aThreshold = (int8_t)ccaConfig.ed_threshold + NRF54L15_MIN_CCA_ED_THRESHOLD - sLnaGain;
    }

    return error;
}

otError otPlatRadioSetCcaEnergyDetectThreshold(otInstance *aInstance, int8_t aThreshold)
{
    OT_UNUSED_VARIABLE(aInstance);

    otError              error = OT_ERROR_NONE;
    nrf_802154_cca_cfg_t ccaConfig;

    aThreshold += sLnaGain;

    // The minimum value of ED threshold for radio driver is -94 dBm
    if (aThreshold < NRF54L15_MIN_CCA_ED_THRESHOLD)
    {
        error = OT_ERROR_INVALID_ARGS;
    }
    else
    {
        memset(&ccaConfig, 0, sizeof(ccaConfig));
        ccaConfig.mode         = NRF_RADIO_CCA_MODE_ED;
        ccaConfig.ed_threshold = nrf_802154_ccaedthres_from_dbm_calculate(aThreshold);

        nrf_802154_cca_cfg_set(&ccaConfig);
    }

    return error;
}

otError otPlatRadioGetFemLnaGain(otInstance *aInstance, int8_t *aGain)
{
    OT_UNUSED_VARIABLE(aInstance);

    otError error = OT_ERROR_NONE;

    if (aGain == NULL)
    {
        error = OT_ERROR_INVALID_ARGS;
    }
    else
    {
        *aGain = sLnaGain;
    }

    return error;
}

otError otPlatRadioSetFemLnaGain(otInstance *aInstance, int8_t aGain)
{
    OT_UNUSED_VARIABLE(aInstance);

    int8_t  threshold;
    int8_t  oldLnaGain = sLnaGain;
    otError error      = OT_ERROR_NONE;

    error = otPlatRadioGetCcaEnergyDetectThreshold(aInstance, &threshold);
    otEXPECT(error == OT_ERROR_NONE);

    sLnaGain = aGain;
    error    = otPlatRadioSetCcaEnergyDetectThreshold(aInstance, threshold);
    otEXPECT_ACTION(error == OT_ERROR_NONE, sLnaGain = oldLnaGain);

exit:
    return error;
}

void nrf5RadioProcess(otInstance *aInstance)
{
    bool isEventPending = false;

    for (uint32_t i = 0; i < NRF_802154_RX_BUFFERS; i++)
    {
        if (sReceivedFrames[i].mPsdu != NULL)
        {
            otPlatRadioReceiveDone(aInstance, &sReceivedFrames[i], OT_ERROR_NONE);

            uint8_t *bufferAddress   = &sReceivedFrames[i].mPsdu[-1];
            sReceivedFrames[i].mPsdu = NULL;
            nrf_802154_buffer_free_raw(bufferAddress);
        }
    }

    if (isPendingEventSet(kPendingEventFrameTransmitted))
    {
        resetPendingEvent(kPendingEventFrameTransmitted);

        otRadioFrame *ackPtr = (sAckFrame.mPsdu == NULL) ? NULL : &sAckFrame;
        g_nrf54_debug_stats.tx_done_success++;
        otPlatRadioTxDone(aInstance, &sTransmitFrame, ackPtr, OT_ERROR_NONE);

        if (sAckFrame.mPsdu != NULL)
        {
            nrf_802154_buffer_free_raw(sAckFrame.mPsdu - 1);
            sAckFrame.mPsdu = NULL;
        }
    }

    if (isPendingEventSet(kPendingEventChannelAccessFailure))
    {
        resetPendingEvent(kPendingEventChannelAccessFailure);
        g_nrf54_debug_stats.tx_done_busy++;
        otPlatRadioTxDone(aInstance, &sTransmitFrame, NULL, OT_ERROR_CHANNEL_ACCESS_FAILURE);
    }

    if (isPendingEventSet(kPendingEventInvalidOrNoAck))
    {
        resetPendingEvent(kPendingEventInvalidOrNoAck);
        g_nrf54_debug_stats.tx_done_no_ack++;
        otPlatRadioTxDone(aInstance, &sTransmitFrame, NULL, OT_ERROR_NO_ACK);
    }

    if (isPendingEventSet(kPendingEventReceiveFailed))
    {
        resetPendingEvent(kPendingEventReceiveFailed);
        otPlatRadioReceiveDone(aInstance, NULL, sReceiveError);
    }

    if (isPendingEventSet(kPendingEventEnergyDetected))
    {
        resetPendingEvent(kPendingEventEnergyDetected);

        otPlatRadioEnergyScanDone(aInstance, sEnergyDetected);
    }

    if (isPendingEventSet(kPendingEventSleep))
    {
        if (nrf_802154_sleep_if_idle() == NRF_802154_SLEEP_ERROR_NONE)
        {
            nrf5FemDisable();
            resetPendingEvent(kPendingEventSleep);
            SetRadioDriverState(NRF_802154_STATE_SLEEP);
        }
        else
        {
            isEventPending = true;
        }
    }

    if (isPendingEventSet(kPendingEventEnergyDetectionStart))
    {
        nrf_802154_channel_set(sEnergyDetectionChannel);

        if (nrf_802154_energy_detection(sEnergyDetectionTime))
        {
            resetPendingEvent(kPendingEventEnergyDetectionStart);
            SetRadioDriverState(NRF_802154_STATE_ENERGY_DETECTION);
        }
        else
        {
            isEventPending = true;
        }
    }

    if (isEventPending)
    {
        otSysEventSignalPending();
    }
}

void nrf_802154_received_timestamp_raw(uint8_t *p_data, int8_t power, uint8_t lqi, uint64_t time)
{
    otRadioFrame *receivedFrame = NULL;

    g_nrf54_debug_stats.rx_frame++;
    SetRadioDriverState(NRF_802154_STATE_RECEIVE);

    for (uint32_t i = 0; i < NRF_802154_RX_BUFFERS; i++)
    {
        if (sReceivedFrames[i].mPsdu == NULL)
        {
            receivedFrame = &sReceivedFrames[i];

            memset(receivedFrame, 0, sizeof(*receivedFrame));
            break;
        }
    }

    assert(receivedFrame != NULL);

    receivedFrame->mPsdu               = &p_data[1];
    receivedFrame->mLength             = p_data[0];
    receivedFrame->mInfo.mRxInfo.mRssi = power;
    receivedFrame->mInfo.mRxInfo.mLqi  = lqi;
    receivedFrame->mChannel            = nrf_802154_channel_get();

    // Inform if this frame was acknowledged with frame pending set.
    if (p_data[ACK_REQUEST_OFFSET] & ACK_REQUEST_BIT)
    {
        receivedFrame->mInfo.mRxInfo.mAckedWithFramePending = sAckedWithFramePending;
    }
    else
    {
        receivedFrame->mInfo.mRxInfo.mAckedWithFramePending = false;
    }

    // Get the timestamp when the SFD was received.
#if !NRF_802154_TX_STARTED_NOTIFY_ENABLED
#error "NRF_802154_TX_STARTED_NOTIFY_ENABLED is required!"
#endif
    receivedFrame->mInfo.mRxInfo.mTimestamp = GetRxTimestamp(time, p_data[0]);

    sAckedWithFramePending = false;

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
    // Inform if this frame was acknowledged with secured Enh-ACK.
    if (p_data[ACK_REQUEST_OFFSET] & ACK_REQUEST_BIT && otMacFrameIsVersion2015(receivedFrame))
    {
        receivedFrame->mInfo.mRxInfo.mAckedWithSecEnhAck = sAckedWithSecEnhAck;
        receivedFrame->mInfo.mRxInfo.mAckFrameCounter    = sAckFrameCounter;
        receivedFrame->mInfo.mRxInfo.mAckKeyId           = sAckKeyId;
    }

    sAckedWithSecEnhAck = false;
#endif

    otSysEventSignalPending();
}

void nrf_802154_receive_failed(nrf_802154_rx_error_t error, uint32_t id)
{
    OT_UNUSED_VARIABLE(id);

    switch (error)
    {
    case NRF_802154_RX_ERROR_INVALID_FRAME:
    case NRF_802154_RX_ERROR_DELAYED_TIMEOUT:
        sReceiveError = OT_ERROR_NO_FRAME_RECEIVED;
        break;

    case NRF_802154_RX_ERROR_INVALID_FCS:
        sReceiveError = OT_ERROR_FCS;
        break;

    case NRF_802154_RX_ERROR_INVALID_DEST_ADDR:
        sReceiveError = OT_ERROR_DESTINATION_ADDRESS_FILTERED;
        break;

    case NRF_802154_RX_ERROR_RUNTIME:
    case NRF_802154_RX_ERROR_TIMESLOT_ENDED:
    case NRF_802154_RX_ERROR_ABORTED:
    case NRF_802154_RX_ERROR_DELAYED_TIMESLOT_DENIED:
    case NRF_802154_RX_ERROR_INVALID_LENGTH:
    case NRF_802154_RX_ERROR_DELAYED_ABORTED:
        sReceiveError = OT_ERROR_FAILED;
        break;

    default:
        assert(false);
    }

    sAckedWithFramePending = false;
#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
    sAckedWithSecEnhAck = false;
#endif

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
    if ((error == NRF_802154_RX_ERROR_DELAYED_TIMEOUT) || (error == NRF_802154_RX_ERROR_TIMESLOT_ENDED))
    {
        sReceiveError = OT_ERROR_NONE;
        setPendingEvent(kPendingEventSleep);
    }
    else
#endif
    {
        setPendingEvent(kPendingEventReceiveFailed);
    }
}

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
static uint16_t getCslPhase(void)
{
    uint32_t curTime       = otPlatAlarmMicroGetNow();
    uint32_t cslPeriodInUs = sCslPeriod * OT_US_PER_TEN_SYMBOLS;
    uint32_t diff = (cslPeriodInUs - (curTime % cslPeriodInUs) + (sCslSampleTime % cslPeriodInUs)) % cslPeriodInUs;
    return (uint16_t)(diff / OT_US_PER_TEN_SYMBOLS + 1);
}
#endif

void nrf_802154_tx_ack_started(const uint8_t *p_data)
{
    otRadioFrame ackFrame;
#if OPENTHREAD_CONFIG_MLE_LINK_METRICS_SUBJECT_ENABLE
    uint8_t      linkMetricsDataLen = 0;
    uint8_t      linkMetricsData[OT_ENH_PROBING_IE_DATA_MAX_SIZE];
    otMacAddress macAddress;
#endif

    OT_UNUSED_VARIABLE(ackFrame);

    ackFrame.mPsdu   = (uint8_t *)(p_data + 1);
    ackFrame.mLength = p_data[0];

    // Check if the frame pending bit is set in ACK frame.
    sAckedWithFramePending = p_data[FRAME_PENDING_OFFSET] & FRAME_PENDING_BIT;

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
    // Update IE and secure Enh-ACK.
#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
    if (sCslPeriod > 0)
    {
        otMacFrameSetCslIe(&ackFrame, sCslPeriod, getCslPhase());
    }
#endif

#if OPENTHREAD_CONFIG_MLE_LINK_METRICS_SUBJECT_ENABLE
    otMacFrameGetDstAddr(&ackFrame, &macAddress);
    // nRF54 driver no longer passes RSSI/LQI in this callout; use placeholders for POC.
    if ((linkMetricsDataLen = otLinkMetricsEnhAckGenData(&macAddress, 0, 0, linkMetricsData)) > 0)
    {
        otMacFrameSetEnhAckProbingIe(&ackFrame, linkMetricsData, linkMetricsDataLen);
    }
#endif

    txAckProcessSecurity((uint8_t *)p_data);
#endif
}

void nrf_802154_transmitted_raw(uint8_t                                   *p_frame,
                                const nrf_802154_transmit_done_metadata_t *p_metadata)
{
    uint8_t *ackPsdu = p_metadata->data.transmitted.p_ack;

    assert(p_frame == sTransmitPsdu);

    SetRadioDriverState(NRF_802154_STATE_RECEIVE);

    if (ackPsdu == NULL)
    {
        g_nrf54_debug_stats.tx_driver_success_no_ack++;
        g_nrf54_debug_stats.last_ack_present = 0;
        sAckFrame.mPsdu = NULL;
    }
    else
    {
        g_nrf54_debug_stats.tx_driver_success_ack++;
        g_nrf54_debug_stats.last_ack_present = 1;
        sAckFrame.mInfo.mRxInfo.mTimestamp = GetRxTimestamp(p_metadata->data.transmitted.time, ackPsdu[0]);
        sAckFrame.mPsdu                    = &ackPsdu[1];
        sAckFrame.mLength                  = ackPsdu[0];
        sAckFrame.mInfo.mRxInfo.mRssi      = p_metadata->data.transmitted.power;
        sAckFrame.mInfo.mRxInfo.mLqi       = p_metadata->data.transmitted.lqi;
        sAckFrame.mChannel                 = nrf_802154_channel_get();
    }

    setPendingEvent(kPendingEventFrameTransmitted);
}

void nrf_802154_transmit_failed(uint8_t                                   *p_frame,
                                nrf_802154_tx_error_t                      error,
                                const nrf_802154_transmit_done_metadata_t *p_metadata)
{
    OT_UNUSED_VARIABLE(p_metadata);
    assert(p_frame == sTransmitPsdu);

    SetRadioDriverState(NRF_802154_STATE_RECEIVE);
    g_nrf54_debug_stats.last_driver_error = error;

    if (error == NRF_802154_TX_ERROR_BUSY_CHANNEL)
    {
        g_nrf54_debug_stats.tx_driver_busy++;
    }
    else if (error == NRF_802154_TX_ERROR_NO_ACK)
    {
        g_nrf54_debug_stats.tx_driver_no_ack++;
    }
    else if (error == NRF_802154_TX_ERROR_TIMESLOT_ENDED)
    {
        g_nrf54_debug_stats.tx_driver_timeslot_ended++;
    }
    else if (error == NRF_802154_TX_ERROR_ABORTED)
    {
        g_nrf54_debug_stats.tx_driver_aborted++;
    }
    else if (error == NRF_802154_TX_ERROR_TIMESLOT_DENIED)
    {
        g_nrf54_debug_stats.tx_driver_timeslot_denied++;
    }

    switch (error)
    {
    case NRF_802154_TX_ERROR_BUSY_CHANNEL:
        // #region agent log
        g_nrf54_debug_stats.tx_fail_busy_channel++;
        // #endregion
        /* fall through */
    case NRF_802154_TX_ERROR_TIMESLOT_ENDED:
    case NRF_802154_TX_ERROR_ABORTED:
    case NRF_802154_TX_ERROR_TIMESLOT_DENIED:
        setPendingEvent(kPendingEventChannelAccessFailure);
        break;

    case NRF_802154_TX_ERROR_INVALID_ACK:
    case NRF_802154_TX_ERROR_NO_ACK:
    case NRF_802154_TX_ERROR_NO_MEM:
        setPendingEvent(kPendingEventInvalidOrNoAck);
        break;

    default:
        assert(false);
    }
}

void nrf_802154_energy_detected(const nrf_802154_energy_detected_t *p_result)
{
    sEnergyDetected = p_result->ed_dbm;
    SetRadioDriverState(NRF_802154_STATE_SLEEP);

    setPendingEvent(kPendingEventEnergyDetected);
}

int8_t otPlatRadioGetReceiveSensitivity(otInstance *aInstance)
{
    OT_UNUSED_VARIABLE(aInstance);

    return NRF54L15_RECEIVE_SENSITIVITY;
}

#if OPENTHREAD_CONFIG_MAC_HEADER_IE_SUPPORT
void nrf_802154_tx_started(const uint8_t *aFrame)
{
    assert(aFrame == sTransmitPsdu);
    OT_UNUSED_VARIABLE(aFrame);

    /* nRF52 model: AES-CCM here, just before on-air TX (core hooks call this in CSMA path). */
    g_nrf54_debug_stats.tx_late_encrypt_hook_enter++;
    nrf54ProcessTransmitSecurity(&sTransmitFrame);
}
#endif

void nrf_802154_random_init(void)
{
    // Intentionally empty
}

void nrf_802154_random_deinit(void)
{
    // Intentionally empty
}

uint32_t nrf_802154_random_get(void)
{
    return otRandomNonCryptoGetUint32();
}

uint64_t otPlatRadioGetNow(otInstance *aInstance)
{
    OT_UNUSED_VARIABLE(aInstance);

    return otPlatTimeGet();
}

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
void otPlatRadioSetMacKey(otInstance             *aInstance,
                          uint8_t                 aKeyIdMode,
                          uint8_t                 aKeyId,
                          const otMacKeyMaterial *aPrevKey,
                          const otMacKeyMaterial *aCurrKey,
                          const otMacKeyMaterial *aNextKey,
                          otRadioKeyType          aKeyType)
{
    OT_UNUSED_VARIABLE(aInstance);
    OT_UNUSED_VARIABLE(aKeyIdMode);

    assert(aKeyType == OT_KEY_TYPE_LITERAL_KEY);
    assert(aPrevKey != NULL && aCurrKey != NULL && aNextKey != NULL);

    NRFX_CRITICAL_SECTION_ENTER();

    sKeyId               = aKeyId;
    sPrevKey             = *aPrevKey;
    sCurrKey             = *aCurrKey;
    sNextKey             = *aNextKey;
    sPrevMacFrameCounter = sMacFrameCounter;

    NRFX_CRITICAL_SECTION_EXIT();
}

void otPlatRadioSetMacFrameCounter(otInstance *aInstance, uint32_t aMacFrameCounter)
{
    OT_UNUSED_VARIABLE(aInstance);

    NRFX_CRITICAL_SECTION_ENTER();

    sMacFrameCounter = aMacFrameCounter;

    NRFX_CRITICAL_SECTION_EXIT();
}

void otPlatRadioSetMacFrameCounterIfLarger(otInstance *aInstance, uint32_t aMacFrameCounter)
{
    OT_UNUSED_VARIABLE(aInstance);

    NRFX_CRITICAL_SECTION_ENTER();

    if (aMacFrameCounter > sMacFrameCounter)
    {
        sMacFrameCounter = aMacFrameCounter;
    }

    NRFX_CRITICAL_SECTION_EXIT();
}
#endif // OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE || OPENTHREAD_CONFIG_MLE_LINK_METRICS_SUBJECT_ENABLE
static void updateIeData(otInstance *aInstance, otShortAddress aShortAddr, const otExtAddress *aExtAddr)
{
    OT_UNUSED_VARIABLE(aInstance);

    int8_t  offset = 0;
    uint8_t ackIeData[OT_ACK_IE_MAX_SIZE];
    uint8_t extAddr[OT_EXT_ADDRESS_SIZE];
    uint8_t shortAddr[SHORT_ADDRESS_SIZE];
#if OPENTHREAD_CONFIG_MLE_LINK_METRICS_SUBJECT_ENABLE
    uint8_t      enhAckProbingDataLen = 0;
    otMacAddress macAddress;
    macAddress.mType                  = OT_MAC_ADDRESS_TYPE_SHORT;
    macAddress.mAddress.mShortAddress = aShortAddr;
#endif

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
    if (sCslPeriod > 0)
    {
        memcpy(ackIeData, sCslIeHeader, OT_IE_HEADER_SIZE);
        offset += OT_IE_HEADER_SIZE + OT_CSL_IE_SIZE; // reserve space for CSL IE
    }
#endif

#if OPENTHREAD_CONFIG_MLE_LINK_METRICS_SUBJECT_ENABLE
    if ((enhAckProbingDataLen = otLinkMetricsEnhAckGetDataLen(&macAddress)) > 0)
    {
        offset += otMacFrameGenerateEnhAckProbingIe(ackIeData + offset, NULL, enhAckProbingDataLen);
    }
#endif

    convertShortAddress(shortAddr, aShortAddr);
    convertExtAddress(extAddr, aExtAddr);

    if (offset > 0)
    {
        nrf_802154_ack_data_set(shortAddr, false, ackIeData, offset, NRF_802154_ACK_DATA_IE);
        nrf_802154_ack_data_set(extAddr, true, ackIeData, offset, NRF_802154_ACK_DATA_IE);
    }
    else
    {
        nrf_802154_ack_data_clear(shortAddr, false, NRF_802154_ACK_DATA_IE);
        nrf_802154_ack_data_clear(extAddr, true, NRF_802154_ACK_DATA_IE);
    }
}
#endif // OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE || OPENTHREAD_CONFIG_MLE_LINK_METRICS_SUBJECT_ENABLE

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
otError otPlatRadioEnableCsl(otInstance         *aInstance,
                             uint32_t            aCslPeriod,
                             otShortAddress      aShortAddr,
                             const otExtAddress *aExtAddr)
{
    sCslPeriod = aCslPeriod;

    updateIeData(aInstance, aShortAddr, aExtAddr);

    return OT_ERROR_NONE;
}

void otPlatRadioUpdateCslSampleTime(otInstance *aInstance, uint32_t aCslSampleTime)
{
    OT_UNUSED_VARIABLE(aInstance);

    sCslSampleTime = aCslSampleTime;
}
#endif // OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE || OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE
uint8_t otPlatRadioGetCslAccuracy(otInstance *aInstance)
{
    OT_UNUSED_VARIABLE(aInstance);

    return otPlatTimeGetXtalAccuracy() / 2;
}

uint8_t otPlatRadioGetCslUncertainty(otInstance *aInstance)
{
    OT_UNUSED_VARIABLE(aInstance);

    return CSL_UNCERT;
}
#endif // OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE || OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE

#if OPENTHREAD_CONFIG_MLE_LINK_METRICS_SUBJECT_ENABLE
otError otPlatRadioConfigureEnhAckProbing(otInstance          *aInstance,
                                          otLinkMetrics        aLinkMetrics,
                                          const otShortAddress aShortAddress,
                                          const otExtAddress  *aExtAddress)
{
    OT_UNUSED_VARIABLE(aInstance);
    OT_UNUSED_VARIABLE(aLinkMetrics);
    OT_UNUSED_VARIABLE(aShortAddress);
    OT_UNUSED_VARIABLE(aExtAddress);

    otError error = OT_ERROR_NONE;

    error = otLinkMetricsConfigureEnhAckProbing(aShortAddress, aExtAddress, aLinkMetrics);
    otEXPECT(error == OT_ERROR_NONE);
    updateIeData(aInstance, aShortAddress, aExtAddress);

exit:
    return error;
}
#endif

otError otPlatRadioSetChannelMaxTransmitPower(otInstance *aInstance, uint8_t aChannel, int8_t aMaxPower)
{
    OT_UNUSED_VARIABLE(aInstance);
    otError error = OT_ERROR_NONE;

    otEXPECT_ACTION(aChannel >= OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MIN && aChannel <= OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MAX,
                    error = OT_ERROR_INVALID_ARGS);

    sMaxTxPowerTable[aChannel - OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MIN] = aMaxPower;
    if (aChannel == nrf_802154_channel_get())
    {
        nrf_802154_tx_power_set(GetTransmitPowerForChannel(aChannel));
    }

exit:
    return error;
}

int8_t nrf5GetChannelMaxTransmitPower(uint8_t aChannel)
{
    int8_t power;

    if (aChannel < OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MIN || aChannel > OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MAX)
    {
        power = OT_RADIO_POWER_INVALID;
    }
    else
    {
        power = sMaxTxPowerTable[aChannel - OT_RADIO_2P4GHZ_OQPSK_CHANNEL_MIN];
    }

    return power;
}

otError otPlatRadioSetRegion(otInstance *aInstance, uint16_t aRegionCode)
{
    OT_UNUSED_VARIABLE(aInstance);

    sRegionCode = aRegionCode;
    nrf5HandleRegionChanged(aRegionCode);
    return OT_ERROR_NONE;
}

otError otPlatRadioGetRegion(otInstance *aInstance, uint16_t *aRegionCode)
{
    OT_UNUSED_VARIABLE(aInstance);
    otError error = OT_ERROR_NONE;

    otEXPECT_ACTION(aRegionCode != NULL, error = OT_ERROR_INVALID_ARGS);

    *aRegionCode = sRegionCode;
exit:
    return error;
}

OT_TOOL_WEAK void nrf5HandleRegionChanged(uint16_t aRegionCode)
{
    OT_UNUSED_VARIABLE(aRegionCode);
}
