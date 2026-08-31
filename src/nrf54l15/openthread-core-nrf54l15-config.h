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
 *   Minimal OpenThread configuration for nRF54L15 RCP bring-up.
 *   Extend as platform features are ported.
 */

#ifndef OPENTHREAD_CORE_NRF54L15_CONFIG_H_
#define OPENTHREAD_CORE_NRF54L15_CONFIG_H_

#undef PACKAGE

#ifndef OPENTHREAD_CONFIG_LOG_OUTPUT
#define OPENTHREAD_CONFIG_LOG_OUTPUT OPENTHREAD_CONFIG_LOG_OUTPUT_PLATFORM_DEFINED
#endif

#ifndef OPENTHREAD_CONFIG_PLATFORM_INFO
#define OPENTHREAD_CONFIG_PLATFORM_INFO "NRF54L15"
#endif

#ifndef OPENTHREAD_CONFIG_STACK_VENDOR_OUI
#define OPENTHREAD_CONFIG_STACK_VENDOR_OUI 0xf4ce36
#endif

#ifndef OPENTHREAD_CONFIG_MLE_MAX_CHILDREN
#define OPENTHREAD_CONFIG_MLE_MAX_CHILDREN 32
#endif

#ifndef OPENTHREAD_CONFIG_MAC_SOFTWARE_ACK_TIMEOUT_ENABLE
#define OPENTHREAD_CONFIG_MAC_SOFTWARE_ACK_TIMEOUT_ENABLE 0
#endif

#ifndef OPENTHREAD_CONFIG_MAC_SOFTWARE_RETRANSMIT_ENABLE
#define OPENTHREAD_CONFIG_MAC_SOFTWARE_RETRANSMIT_ENABLE 1
#endif

#ifndef OPENTHREAD_CONFIG_MAC_SOFTWARE_CSMA_BACKOFF_ENABLE
#define OPENTHREAD_CONFIG_MAC_SOFTWARE_CSMA_BACKOFF_ENABLE 0
#endif

#ifndef OPENTHREAD_CONFIG_MAC_SOFTWARE_TX_SECURITY_ENABLE
#define OPENTHREAD_CONFIG_MAC_SOFTWARE_TX_SECURITY_ENABLE 1
#endif

#ifndef OPENTHREAD_CONFIG_MAC_SOFTWARE_TX_TIMING_ENABLE
#define OPENTHREAD_CONFIG_MAC_SOFTWARE_TX_TIMING_ENABLE 1
#endif

#ifndef OPENTHREAD_CONFIG_MAC_SOFTWARE_RX_TIMING_ENABLE
#define OPENTHREAD_CONFIG_MAC_SOFTWARE_RX_TIMING_ENABLE 1
#endif

#ifndef OPENTHREAD_CONFIG_PLATFORM_USEC_TIMER_ENABLE
#define OPENTHREAD_CONFIG_PLATFORM_USEC_TIMER_ENABLE 1
#endif

#ifndef RADIO_CONFIG_SRC_MATCH_SHORT_ENTRY_NUM
#define RADIO_CONFIG_SRC_MATCH_SHORT_ENTRY_NUM 0
#endif

#ifndef RADIO_CONFIG_SRC_MATCH_EXT_ENTRY_NUM
#define RADIO_CONFIG_SRC_MATCH_EXT_ENTRY_NUM 0
#endif

/* AHEAD/AFTER match the nRF defconfig in NCS (nrf/subsys/net/openthread/Kconfig.defconfig): the
 * 802.15.4 driver extends the DRX window itself once it detects a start of frame inside it. */
#ifndef OPENTHREAD_CONFIG_MIN_RECEIVE_ON_AHEAD
#define OPENTHREAD_CONFIG_MIN_RECEIVE_ON_AHEAD 104
#endif

/* Lead time for SubMac to call ReceiveAt before winStart. Matches CONFIG_OPENTHREAD_CSL_RECEIVE_TIME_AHEAD
 * in NCS (zephyr/modules/openthread/Kconfig.thread), which the nRF defconfig does not override. */
#ifndef OPENTHREAD_CONFIG_CSL_RECEIVE_TIME_AHEAD
#define OPENTHREAD_CONFIG_CSL_RECEIVE_TIME_AHEAD 5000
#endif

#ifndef OPENTHREAD_CONFIG_MIN_RECEIVE_ON_AFTER
#define OPENTHREAD_CONFIG_MIN_RECEIVE_ON_AFTER 0
#endif

#ifndef OPENTHREAD_CONFIG_MAC_CSL_DEBUG_ENABLE
#define OPENTHREAD_CONFIG_MAC_CSL_DEBUG_ENABLE 0
#endif

/**
 * @def OPENTHREAD_CONFIG_MLE_CSL_ATTACH_FAIL_STOP_THREAD_ENABLE
 *
 * Stop Thread if the first Child Update Request after attach gets no Child Update Response (CSL sync failure).
 */
#ifndef OPENTHREAD_CONFIG_MLE_CSL_ATTACH_FAIL_STOP_THREAD_ENABLE
#define OPENTHREAD_CONFIG_MLE_CSL_ATTACH_FAIL_STOP_THREAD_ENABLE 1
#endif

/**
 * @def NRF54_CSL_KEEP_RADIO_AWAKE
 *
 * When set, the nRF54 radio platform skips sleep while CSL is enabled (`sCslPeriod > 0`):
 * no sleep after DRX windows, after mesh TX, from `otPlatRadioSleep`, or `SetRxOnWhenIdle(false)`.
 * Default 0 (NCS-like): platform may sleep after DRX windows and between CSL schedules.
 * Set to 1 only for debug (keeps mesh RX; reproduces skipped_already_rx). Override at build:
 * `-DNRF54_CSL_KEEP_RADIO_AWAKE=1` or env in `build-nrf54l15-cli-ftd-csl-debug`.
 */
#ifndef NRF54_CSL_KEEP_RADIO_AWAKE
#define NRF54_CSL_KEEP_RADIO_AWAKE 0
#endif

/**
 * @def OPENTHREAD_CONFIG_MAC_HEADER_IE_SUPPORT
 *
 * Required for platform TX security (nrf54ProcessTransmitSecurity / otMacFrameProcessTransmitAesCcm)
 * and Enh-ACK / CSL IE handling — same as nRF52840.
 */
#ifndef OPENTHREAD_CONFIG_MAC_HEADER_IE_SUPPORT
#define OPENTHREAD_CONFIG_MAC_HEADER_IE_SUPPORT 1
#endif

#ifndef OPENTHREAD_CONFIG_PLATFORM_FLASH_API_ENABLE
#define OPENTHREAD_CONFIG_PLATFORM_FLASH_API_ENABLE 1
#endif

/**
 * @def OPENTHREAD_CONFIG_HEAP_INTERNAL_SIZE
 *
 * The size of heap buffer when DTLS is enabled.
 */
#ifndef OPENTHREAD_CONFIG_HEAP_INTERNAL_SIZE
#define OPENTHREAD_CONFIG_HEAP_INTERNAL_SIZE (4096 * sizeof(void *))
#endif

/**
 * @def OPENTHREAD_CONFIG_HEAP_INTERNAL_SIZE_NO_DTLS
 *
 * The size of heap buffer when DTLS is disabled.
 */
#ifndef OPENTHREAD_CONFIG_HEAP_INTERNAL_SIZE_NO_DTLS
#define OPENTHREAD_CONFIG_HEAP_INTERNAL_SIZE_NO_DTLS 4000
#endif

/**
 * @def OPENTHREAD_CONFIG_CLI_UART_TX_BUFFER_SIZE
 *
 * The size of CLI message buffer in bytes.
 */
#ifndef OPENTHREAD_CONFIG_CLI_UART_TX_BUFFER_SIZE
#define OPENTHREAD_CONFIG_CLI_UART_TX_BUFFER_SIZE 2048
#endif

#endif // OPENTHREAD_CORE_NRF54L15_CONFIG_H_
