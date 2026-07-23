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
 *   Minimal nRF54L15 platform configuration for 802.15.4 radio driver bring-up.
 *
 *   Alarm, UART, flash and FEM sections will be added when platform code is ported.
 */

#ifndef PLATFORM_CONFIG_H_
#define PLATFORM_CONFIG_H_

#include "openthread-core-config.h"
#include <openthread/config.h>

/*******************************************************************************
 * @section Radio driver configuration.
 ******************************************************************************/

#ifndef NRF_802154_PENDING_SHORT_ADDRESSES
#define NRF_802154_PENDING_SHORT_ADDRESSES OPENTHREAD_CONFIG_MLE_MAX_CHILDREN
#endif

#ifndef NRF_802154_PENDING_EXTENDED_ADDRESSES
#define NRF_802154_PENDING_EXTENDED_ADDRESSES OPENTHREAD_CONFIG_MLE_MAX_CHILDREN
#endif

#ifndef NRF_802154_CSMA_CA_ENABLED
#define NRF_802154_CSMA_CA_ENABLED 1
#endif

#ifndef NRF_802154_ACK_TIMEOUT_ENABLED
#define NRF_802154_ACK_TIMEOUT_ENABLED 1
#endif

#ifndef NRF_802154_TX_STARTED_NOTIFY_ENABLED
#define NRF_802154_TX_STARTED_NOTIFY_ENABLED 1
#endif

#ifndef PLATFORM_FEM_ENABLE_DEFAULT_CONFIG
#define PLATFORM_FEM_ENABLE_DEFAULT_CONFIG 0
#endif

/*******************************************************************************
 * @section OpenThread alarm (GRTC) configuration.
 ******************************************************************************/

/**
 * @def OT_GRTC_IRQ_PRIORITY
 *
 * Interrupt priority for GRTC (OT ms/us alarms).
 */
#ifndef OT_GRTC_IRQ_PRIORITY
#define OT_GRTC_IRQ_PRIORITY 6
#endif

/**
 * @def OT_GRTC_US_PER_TICK
 *
 * GRTC SYSCOUNTER tick period in microseconds (1 MHz counter on nRF54L15).
 */
#ifndef OT_GRTC_US_PER_TICK
#define OT_GRTC_US_PER_TICK 1ULL
#endif

/**
 * @def OT_GRTC_CC_MS
 * @def OT_GRTC_CC_US
 * @def OT_GRTC_CC_RADIO_TIMER
 * @def OT_GRTC_CC_RADIO_SYNC
 *
 * Logical names for the GRTC compare-channel budget (documentation only).
 * Runtime channel numbers come from nrfx_grtc_channel_alloc() and must be stored
 * in module-local variables (see alarm_nrf54.c, nrf_802154_platform_sl_lptimer.c).
 * Do not assert or compare alloc results against these constants.
 *
 * All consumers must fit in NRFX_GRTC_CONFIG_ALLOWED_CC_CHANNELS_MASK
 * (default 0x00000f0f in nrfx_config_nrf54l15_application.h: CC0–3 and CC8–11).
 */
#ifndef OT_GRTC_CC_MS
#define OT_GRTC_CC_MS 0
#endif

#ifndef OT_GRTC_CC_US
#define OT_GRTC_CC_US 1
#endif

#ifndef OT_GRTC_CC_RADIO_TIMER
#define OT_GRTC_CC_RADIO_TIMER 2
#endif

#ifndef OT_GRTC_CC_RADIO_SYNC
#define OT_GRTC_CC_RADIO_SYNC 3
#endif

/**
 * @def OT_GRTC_CC_RADIO_HW_TASK
 *
 * Logical name for the 802.15.4 SL hardware-triggered radio task compare channel
 * (DPPI). CC8 must be allowed in NRFX_GRTC_CONFIG_ALLOWED_CC_CHANNELS_MASK
 * (0x00000f0f). The actual channel index is returned by nrfx_grtc_channel_alloc().
 */
#ifndef OT_GRTC_CC_RADIO_HW_TASK
#define OT_GRTC_CC_RADIO_HW_TASK 8
#endif

/**
 * @def OT_XTAL_ACCURACY
 *
 * Crystal accuracy for otPlatTimeGetXtalAccuracy() [ppm * 2].
 */
#ifndef OT_XTAL_ACCURACY
#define OT_XTAL_ACCURACY 40
#endif

#endif // PLATFORM_CONFIG_H_
