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
#include <nrf_802154_common_utils.h>
#include <nrf_802154_const.h>
#include <nrf_802154_pib.h>
#include "platform/nrf_802154_clock.h"

#include <openthread/random_noncrypto.h>

// clang-format off

#define SHORT_ADDRESS_SIZE    2            ///< Size of MAC short address.
#define US_PER_MS             1000ULL      ///< Microseconds in millisecond.

#define RSSI_SETTLE_TIME_US   40           ///< RSSI settle time in microseconds.
#define DRX_SLOT_RX           0            ///< Delayed reception window ID for CSL.
#define PHR_DURATION_US       32           ///< Duration of the PHR field.

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
static bool          sCslHfclkHeld; /* CSL-F1: HFCLK ref-count while CSL active */
static const uint8_t sCslIeHeader[OT_IE_HEADER_SIZE] = {CSL_IE_HEADER_BYTES_LO, CSL_IE_HEADER_BYTES_HI};


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
static bool     sRxOnWhenIdle = true;

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
static uint32_t sMacFrameCounter;
static bool     sAckedWithSecEnhAck;
static uint32_t sAckFrameCounter;
static uint8_t  sAckKeyId;
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

/* The driver timestamps the end of the last symbol; OT expects the start of the PHR. */
static uint64_t GetRxTimestamp(uint64_t aTime, uint8_t aLength)
{
    if (aTime == NRF_802154_NO_TIMESTAMP)
    {
        return nrf5AlarmGetCurrentTime();
    }

    return nrf_802154_timestamp_end_to_phr_convert(aTime, aLength);
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

    sRxOnWhenIdle = true;

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

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
static inline bool nrf54CslSuppressPlatformSleep(void)
{
#if NRF54_CSL_KEEP_RADIO_AWAKE
    return sCslPeriod > 0;
#else
    return false;
#endif
}


/**
 * Enter driver sleep for sleepy CSL child (NCS contract). Uses sleep_if_idle first;
 * if still in mesh RX, terminate with nrf_802154_sleep() so DRX core_receive is not skipped.
 *
 * @returns true if radio is in sleep (or was already).
 */
static bool nrf54CslTryEnterSleep(void)
{
    nrf_802154_sleep_error_t err;

    err = nrf_802154_sleep_if_idle();

    if (err == NRF_802154_SLEEP_ERROR_NONE)
    {
        nrf5FemDisable();
        SetRadioDriverState(NRF_802154_STATE_SLEEP);
        return true;
    }

    if (sCslPeriod > 0 && !sRxOnWhenIdle)
    {
        err = nrf_802154_sleep();

        if (err == NRF_802154_SLEEP_ERROR_NONE)
        {
            nrf5FemDisable();
            SetRadioDriverState(NRF_802154_STATE_SLEEP);
            return true;
        }
    }

    return false;
}

/* CSL-P0-F3b: after poll on mesh channel, leave HW RX so DRX callback is not skipped. */
static void cslScheduleSleepIfChildRxOff(void)
{
    if (nrf54CslSuppressPlatformSleep())
    {
        return;
    }

    if ((sCslPeriod > 0) && !sRxOnWhenIdle && !IsRadioDriverStateSleep())
    {
        setPendingEvent(kPendingEventSleep);
    }
}
#endif

static bool nrfRadioTryEnterSleep(void)
{
#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
    return nrf54CslTryEnterSleep();
#else
    if (nrf_802154_sleep_if_idle() == NRF_802154_SLEEP_ERROR_NONE)
    {
        nrf5FemDisable();
        SetRadioDriverState(NRF_802154_STATE_SLEEP);
        return true;
    }

    return false;
#endif
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

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
    if (nrf54CslSuppressPlatformSleep())
    {
        return OT_ERROR_NONE;
    }
#endif

    if (nrfRadioTryEnterSleep())
    {
        clearPendingEvents();
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

static uint64_t unwrapFutureRadioTimeUs(uint32_t aTimeUs)
{
    uint64_t nowUs = otPlatTimeGet();

    /* 32-bit OT radio time → 64-bit GRTC: signed delta unwrap (same rule as alarm_nrf54.c / 52840 radio.c). */
    return nowUs + (int32_t)(aTimeUs - (uint32_t)nowUs);
}

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
otError otPlatRadioReceiveAt(otInstance *aInstance, uint8_t aChannel, uint32_t aStart, uint32_t aDuration)
{
    OT_UNUSED_VARIABLE(aInstance);

    bool     result;
    uint64_t rxTime = unwrapFutureRadioTimeUs(aStart);

    /* A window in the past is not rejected here: parity with NCS radio_nrf5.c, where a late call
     * still lets the driver decide and keeps the CSL anchor at anchor_time + n * csl_period.
     * The first window after CSL is enabled and the one from RestartCslTimerAfterSyncUpdate() are
     * in the past by construction. */
    nrf_802154_tx_power_set(GetTransmitPowerForChannel(aChannel));
    (void)nrf_802154_receive_at_scheduled_cancel(DRX_SLOT_RX);

    result = nrf_802154_receive_at(rxTime, aDuration, aChannel, DRX_SLOT_RX);
    clearPendingEvents();

    if (result)
    {
        /* After DRX is scheduled, drop mesh RX (not sleep before receive_at — that kills lptimer). */
        if (!sRxOnWhenIdle && sDriverState == NRF_802154_STATE_RECEIVE)
        {
            setPendingEvent(kPendingEventSleep);
        }
    }

    return result ? OT_ERROR_NONE : OT_ERROR_FAILED;
}
#endif

#if OPENTHREAD_CONFIG_MAC_HEADER_IE_SUPPORT
static void nrf54ProcessTimeSyncIe(otRadioFrame *aFrame)
{
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
    }
#endif // OPENTHREAD_CONFIG_TIME_SYNC_ENABLE
}
#endif // OPENTHREAD_CONFIG_MAC_HEADER_IE_SUPPORT

otError otPlatRadioTransmit(otInstance *aInstance, otRadioFrame *aFrame)
{
    nrf_802154_tx_error_t txError = NRF_802154_TX_ERROR_NONE;
    otError               error   = OT_ERROR_NONE;


    aFrame->mPsdu[-1] = aFrame->mLength;

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
    nrf_802154_transmitted_frame_props_t frameProps = {
        .is_secured          = aFrame->mInfo.mTxInfo.mIsSecurityProcessed,
        .dynamic_data_is_set = aFrame->mInfo.mTxInfo.mIsHeaderUpdated,
    };
#else
    nrf_802154_transmitted_frame_props_t frameProps = NRF_802154_TRANSMITTED_FRAME_PROPS_DEFAULT_INIT;
#endif

    if (IsRadioDriverStateSleep())
    {
        // Enable FEM before RADIO leaving SLEEP state.
        nrf5FemEnable();
    }

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
#if OPENTHREAD_CONFIG_MAC_HEADER_IE_SUPPORT
    nrf54ProcessTimeSyncIe(aFrame);
#endif

    if (aFrame->mInfo.mTxInfo.mTxDelay != 0)
    {
#if NRF_802154_DELAYED_TRX_ENABLED
        nrf_802154_transmit_at_metadata_t atMetadata = {
            .frame_props         = frameProps,
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
                .frame_props         = frameProps,
                .tx_power            = {.use_metadata_value = false},
                .tx_channel          = {.use_metadata_value = true, .channel = aFrame->mChannel},
                .tx_timestamp_encode = false,
            };

            (void)nrf_802154_csma_ca_max_backoffs_set(aFrame->mInfo.mTxInfo.mMaxCsmaBackoffs);
            txError = nrf_802154_transmit_csma_ca_raw(&aFrame->mPsdu[-1], &csmaMetadata);
        }
        else
#endif
        {
            /* Match nRF52 radio.c: CCA runs only via the driver CSMA-CA module.
             * transmit_raw() must not honor mCsmaCaEnabled — OT sets it true for
             * data/MLE even when CSMA_CA_ENABLED=0, which caused TxErrCca storms. */
            nrf_802154_transmit_metadata_t metadata = {
                .frame_props         = frameProps,
                .cca                 = false,
                .tx_power            = {.use_metadata_value = false},
                .tx_channel          = {.use_metadata_value = false},
                .tx_timestamp_encode = false,
            };

            txError = nrf_802154_transmit_raw(&aFrame->mPsdu[-1], &metadata);
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
                         OT_RADIO_CAPS_RX_ON_WHEN_IDLE | OT_RADIO_CAPS_SLEEP_TO_TX);
}

void otPlatRadioSetRxOnWhenIdle(otInstance *aInstance, bool aEnable)
{
    OT_UNUSED_VARIABLE(aInstance);

    sRxOnWhenIdle = aEnable;
    nrf_802154_rx_on_when_idle_set(sRxOnWhenIdle);

    if (!sRxOnWhenIdle)
    {
#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
        if (!nrf54CslSuppressPlatformSleep())
        {
            (void)nrf54CslTryEnterSleep();
        }
#else
        (void)nrf_802154_sleep_if_idle();
#endif
    }
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
        otPlatRadioTxDone(aInstance, &sTransmitFrame, ackPtr, OT_ERROR_NONE);

#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
        cslScheduleSleepIfChildRxOff(); /* CSL-P0-F3b */
#endif

        if (sAckFrame.mPsdu != NULL)
        {
            nrf_802154_buffer_free_raw(sAckFrame.mPsdu - 1);
            sAckFrame.mPsdu = NULL;
        }
    }

    if (isPendingEventSet(kPendingEventChannelAccessFailure))
    {
        resetPendingEvent(kPendingEventChannelAccessFailure);
        otPlatRadioTxDone(aInstance, &sTransmitFrame, NULL, OT_ERROR_CHANNEL_ACCESS_FAILURE);
#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
        cslScheduleSleepIfChildRxOff(); /* CSL-P0-F3b */
#endif
    }

    if (isPendingEventSet(kPendingEventInvalidOrNoAck))
    {
        resetPendingEvent(kPendingEventInvalidOrNoAck);
        otPlatRadioTxDone(aInstance, &sTransmitFrame, NULL, OT_ERROR_NO_ACK);
#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
        cslScheduleSleepIfChildRxOff(); /* CSL-P0-F3b */
#endif
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
#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
        if (nrf54CslSuppressPlatformSleep())
        {
            resetPendingEvent(kPendingEventSleep);
        }
        else
#endif
        if (nrfRadioTryEnterSleep())
        {
            resetPendingEvent(kPendingEventSleep);
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
#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
    if (id == DRX_SLOT_RX && error == NRF_802154_RX_ERROR_DELAYED_TIMEOUT)
    {

        sAckedWithFramePending = false;
#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
        sAckedWithSecEnhAck = false;
#endif

        if (nrf54CslSuppressPlatformSleep())
        {
            otSysEventSignalPending();
            return;
        }

        /* CSL-P0-F3a: NCS/baseline contract — sleep after CSL window for all children
         * (including rx-off sleepy child). Replaces early return that blocked sleep. */
        setPendingEvent(kPendingEventSleep);
        otSysEventSignalPending();
        return;
    }
#else
    OT_UNUSED_VARIABLE(id);
#endif

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
#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
        if (!nrf54CslSuppressPlatformSleep())
#endif
        {
            setPendingEvent(kPendingEventSleep);
        }
    }
    else
#endif
    {
        setPendingEvent(kPendingEventReceiveFailed);
    }
}


#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
static void nrf54UpdateTxFrameInfo(const nrf_802154_transmit_done_metadata_t *aMetadata)
{
    sTransmitFrame.mInfo.mTxInfo.mIsSecurityProcessed = aMetadata->frame_props.is_secured;
    sTransmitFrame.mInfo.mTxInfo.mIsHeaderUpdated     = aMetadata->frame_props.dynamic_data_is_set;
}
#endif

void nrf_802154_tx_ack_started(const uint8_t *p_data)
{
    sAckedWithFramePending = p_data[FRAME_PENDING_OFFSET] & FRAME_PENDING_BIT;

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
    otRadioFrame ackFrame;

    sAckedWithSecEnhAck = false;

    otEXPECT(p_data[SECURITY_ENABLED_OFFSET] & SECURITY_ENABLED_BIT);

    memset(&ackFrame, 0, sizeof(ackFrame));
    ackFrame.mPsdu   = (uint8_t *)(p_data + 1);
    ackFrame.mLength = p_data[0];

    otEXPECT(otMacFrameIsKeyIdMode1(&ackFrame) && otMacFrameGetKeyId(&ackFrame) != 0);

    sAckedWithSecEnhAck = true;
    sAckFrameCounter    = otMacFrameGetFrameCounter(&ackFrame);
    sAckKeyId           = otMacFrameGetKeyId(&ackFrame);

exit:
    return;
#endif
}

void nrf_802154_transmitted_raw(uint8_t                                   *p_frame,
                                const nrf_802154_transmit_done_metadata_t *p_metadata)
{
    uint8_t *ackPsdu = p_metadata->data.transmitted.p_ack;

    assert(p_frame == sTransmitPsdu);

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
    nrf54UpdateTxFrameInfo(p_metadata);
#endif

    SetRadioDriverState(NRF_802154_STATE_RECEIVE);

    if (ackPsdu == NULL)
    {
        sAckFrame.mPsdu = NULL;
    }
    else
    {
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
    assert(p_frame == sTransmitPsdu);

#if OPENTHREAD_CONFIG_THREAD_VERSION >= OT_THREAD_VERSION_1_2
    nrf54UpdateTxFrameInfo(p_metadata);
#endif

    SetRadioDriverState(NRF_802154_STATE_RECEIVE);

    switch (error)
    {
    case NRF_802154_TX_ERROR_BUSY_CHANNEL:
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

static void nrf54SecurityKeyStore(uint8_t *aKeyValue, nrf_802154_key_id_mode_t aKeyIdMode, uint8_t *aKeyId)
{
    nrf_802154_key_t key = {
        .value.p_cleartext_key   = aKeyValue,
        .id.mode                 = aKeyIdMode,
        .id.p_key_id             = aKeyId,
        .type                    = NRF_802154_KEY_CLEARTEXT,
        .frame_counter           = 0,
        .use_global_frame_counter = true,
    };

    nrf_802154_security_error_t err = nrf_802154_security_key_store(&key);

    assert(err == NRF_802154_SECURITY_ERROR_NONE || err == NRF_802154_SECURITY_ERROR_ALREADY_PRESENT);
}

void otPlatRadioSetMacKey(otInstance             *aInstance,
                          uint8_t                 aKeyIdMode,
                          uint8_t                 aKeyId,
                          const otMacKeyMaterial *aPrevKey,
                          const otMacKeyMaterial *aCurrKey,
                          const otMacKeyMaterial *aNextKey,
                          otRadioKeyType          aKeyType)
{
    uint8_t keyIdMode = aKeyIdMode >> 3;
    uint8_t prevKeyId = 0;
    uint8_t nextKeyId = 0;

    OT_UNUSED_VARIABLE(aInstance);

    assert(aKeyType == OT_KEY_TYPE_LITERAL_KEY);
    assert(aPrevKey != NULL && aCurrKey != NULL && aNextKey != NULL);

    NRFX_CRITICAL_SECTION_ENTER();

    if (keyIdMode == 1)
    {
        assert(NRF_802154_SECURITY_KEY_STORAGE_SIZE >= 3);

        /* Thread Key ID Mode 1: valid key indices are 1..0x80 with wrap. */
        prevKeyId = (aKeyId == 1) ? 0x80 : (aKeyId - 1);
        nextKeyId = (aKeyId == 0x80) ? 1 : (aKeyId + 1);

        nrf_802154_security_key_remove_all();

        nrf54SecurityKeyStore((uint8_t *)aPrevKey->mKeyMaterial.mKey.m8, keyIdMode, &prevKeyId);
        nrf54SecurityKeyStore((uint8_t *)aCurrKey->mKeyMaterial.mKey.m8, keyIdMode, &aKeyId);
        nrf54SecurityKeyStore((uint8_t *)aNextKey->mKeyMaterial.mKey.m8, keyIdMode, &nextKeyId);
    }
    else
    {
        /* aKeyId == 0 with mode 0: stack reset / key clear (RCP convention). */
        assert(keyIdMode == 0 && aKeyId == 0);

        nrf_802154_security_key_remove_all();
    }

    NRFX_CRITICAL_SECTION_EXIT();
}

void otPlatRadioSetMacFrameCounter(otInstance *aInstance, uint32_t aMacFrameCounter)
{
    OT_UNUSED_VARIABLE(aInstance);

    NRFX_CRITICAL_SECTION_ENTER();

    sMacFrameCounter = aMacFrameCounter;
    nrf_802154_security_global_frame_counter_set(aMacFrameCounter);

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

    nrf_802154_security_global_frame_counter_set_if_larger(aMacFrameCounter);

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

    /* CSL-F1-BEGIN: hold HFCLK between CSL windows (revert F1: remove block through CSL-F1-END) */
    if (aCslPeriod > 0)
    {
        if (!sCslHfclkHeld)
        {
            nrf_802154_clock_hfclk_start();
            sCslHfclkHeld = true;
        }
    }
    else if (sCslHfclkHeld)
    {
        nrf_802154_clock_hfclk_stop();
        sCslHfclkHeld = false;
    }
    /* CSL-F1-END */
#if NRF_802154_DELAYED_TRX_ENABLED && NRF_802154_IE_WRITER_ENABLED
    nrf_802154_csl_writer_period_set((uint16_t)aCslPeriod);
#endif

    updateIeData(aInstance, aShortAddr, aExtAddr);

    return OT_ERROR_NONE;
}

void otPlatRadioUpdateCslSampleTime(otInstance *aInstance, uint32_t aCslSampleTime)
{
    OT_UNUSED_VARIABLE(aInstance);

    sCslSampleTime = aCslSampleTime;
#if NRF_802154_DELAYED_TRX_ENABLED && NRF_802154_IE_WRITER_ENABLED
    /* The CSL sample time points to the start of the MAC header, while the expected RX time refers
     * to the end of the SFD. */
    uint64_t expectedRxTime = unwrapFutureRadioTimeUs(aCslSampleTime - PHR_DURATION_US);

    nrf_802154_csl_writer_anchor_time_set(nrf_802154_timestamp_phr_to_mhr_convert(expectedRxTime));
#endif
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
