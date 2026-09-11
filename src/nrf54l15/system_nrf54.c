/*
 *  Copyright (c) 2016, The OpenThread Authors.
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
 *   Platform init/deinit for nRF54L15 bare-metal RCP.
 *
 *   LFCLK/GRTC: alarm_nrf54.c (nrf_802154_clock + nrfx_grtc).
 *   HFCLK/XO for radio/UART: third_party/nrf54/platform/nrf_802154_clock_platform.c
 *   and transport (when ported). No legacy nrf_drv_clock on nRF54.
 *
 */

#include <openthread-core-config.h>
#include <openthread/config.h>

#include <openthread/platform/logging.h>

#include "openthread-system.h"
#include "perf_timing_port.h"
#include "platform-fem.h"
#include "platform-nrf5-transport.h"
#include "platform-nrf5.h"

#include <openthread/platform/time.h>

#include <nrfx.h>
/* CSL-F4.1-BEGIN: tasklets + early alarm in main loop */
#include <openthread/tasklet.h>
/* CSL-F4.1-END */

/* Weak no-op: overridden by src/ot_perf/ot_perf.c when linked into ot-cli-ftd. */
OT_TOOL_WEAK void otPerfProcess(otInstance *aInstance) { OT_UNUSED_VARIABLE(aInstance); }

#if !OPENTHREAD_CONFIG_ENABLE_BUILTIN_MBEDTLS_MANAGEMENT && PLATFORM_OPENTHREAD_VANILLA

#include <mbedtls/platform.h>
#include <mbedtls/threading.h>

#include <openthread/heap.h>

#endif

extern bool gPlatformPseudoResetWasRequested;

/*
 * The radio ISR only latches a bit; otPlatRadioTxDone/ReceiveDone are handed to OpenThread by
 * nrf5RadioProcess(), and the MAC only acts on them in the following otTaskletsProcess(). Pair the
 * two here, and keep going while the ISR posts more completions, so a burst of them costs one
 * turnaround instead of one loop iteration each.
 */
static void nrf54ServiceRadio(otInstance *aInstance)
{
    /* Bounded so heavy RX cannot starve the rest of the driver pass indefinitely. */
    unsigned drainsLeft = 8;

    do
    {
        nrf5RadioProcess(aInstance);
        otTaskletsProcess(aInstance);
    } while (nrf5RadioHasPendingCallbacks() && drainsLeft-- != 0);
}

void __cxa_pure_virtual(void)
{
    while (1);
}

void otSysInit(int argc, char *argv[])
{
    OT_UNUSED_VARIABLE(argc);
    OT_UNUSED_VARIABLE(argv);

    if (gPlatformPseudoResetWasRequested)
    {
        otSysDeinit();
    }

#if !OPENTHREAD_CONFIG_ENABLE_BUILTIN_MBEDTLS_MANAGEMENT && PLATFORM_OPENTHREAD_VANILLA
    mbedtls_platform_set_calloc_free(otHeapCAlloc, otHeapFree);
    mbedtls_platform_setup(NULL);
#endif

#if (OPENTHREAD_CONFIG_LOG_OUTPUT == OPENTHREAD_CONFIG_LOG_OUTPUT_PLATFORM_DEFINED)
    nrf5LogInit();
#endif
    nrf5AlarmInit();
    nrf5RandomInit();
    nrf5TransportInit(gPlatformPseudoResetWasRequested);
    nrf5MiscInit();
    nrf5RadioInit();
    nrf5TempInit();
    nrf5FemInit();
    nrf5CryptoInit();

    gPlatformPseudoResetWasRequested = false;
}

void otSysDeinit(void)
{
    nrf5FemDeinit();
    nrf5TempDeinit();
    nrf5RadioDeinit();
    nrf5MiscDeinit();
    nrf5CryptoDeinit();
    nrf5TransportDeinit(gPlatformPseudoResetWasRequested);
    nrf5RandomDeinit();
    nrf5AlarmDeinit();
#if (OPENTHREAD_CONFIG_LOG_OUTPUT == OPENTHREAD_CONFIG_LOG_OUTPUT_PLATFORM_DEFINED)
    nrf5LogDeinit();
#endif

#if !OPENTHREAD_CONFIG_ENABLE_BUILTIN_MBEDTLS_MANAGEMENT && PLATFORM_OPENTHREAD_VANILLA
    mbedtls_platform_teardown(NULL);
#endif
}

bool otSysPseudoResetWasRequested(void)
{
    return gPlatformPseudoResetWasRequested;
}

static uint32_t perfTimingElapsedUs(uint64_t *aTimestampUs)
{
    uint64_t now = otPlatTimeGet();
    uint32_t elapsed;

    elapsed = (uint32_t)((now >= *aTimestampUs) ? (now - *aTimestampUs) : 0U);
    *aTimestampUs = now;

    return elapsed;
}

void otSysProcessDrivers(otInstance *aInstance)
{
    const bool     timing = otPerfTimingIsActive();
    uint64_t       timestampUs;
    uint32_t       sectionUs;

    if (timing)
    {
        otPerfTimingDriverPassBegin();
        timestampUs = otPlatTimeGet();
    }

    /* CSL-F4.1-BEGIN: alarm before radio (was last in driver pass) */
    nrf5AlarmProcess(aInstance);
    /* CSL-F4.1-END */

    /*
     * Drain Spinel before delivering deferred radio callbacks so UART HWFC
     * releases the host promptly. Run it again afterwards to complete UART
     * events that may have arrived while OpenThread handled the radio work.
     */
    nrf5TransportProcess();

    if (timing)
    {
        sectionUs = perfTimingElapsedUs(&timestampUs);
        otPerfTimingAddTransportUs(sectionUs);
    }

    /*
     * A transmission that finished while the loop was elsewhere must not queue up behind the pump:
     * otPerfProcess() builds and submits a whole datagram, so serialising it in front of a waiting
     * completion adds its full cost to the inter-frame gap. Release the completion first, then pump
     * while the frame it started is on air.
     */
    if (nrf5RadioHasPendingCallbacks())
    {
        nrf54ServiceRadio(aInstance);

        if (timing)
        {
            sectionUs = perfTimingElapsedUs(&timestampUs);
            otPerfTimingAddRadioPreUs(sectionUs);
        }
    }

    otPerfProcess(aInstance);

    if (timing)
    {
        sectionUs = perfTimingElapsedUs(&timestampUs);
        otPerfTimingAddPerfProcessUs(sectionUs);
    }

    nrf54ServiceRadio(aInstance);

    if (timing)
    {
        sectionUs = perfTimingElapsedUs(&timestampUs);
        otPerfTimingAddRadioPostUs(sectionUs);
    }

    nrf5TransportProcess();

    if (timing)
    {
        sectionUs = perfTimingElapsedUs(&timestampUs);
        otPerfTimingAddTransportUs(sectionUs);
        otPerfTimingDriverPassEnd();
    }

    nrf5TempProcess();
}

/* CSL-F4.1-BEGIN: __SEV() wake (was __WEAK empty stub) */
void otSysEventSignalPending(void)
{
    __SEV();
}
/* CSL-F4.1-END */

/* CSL-F4.1-BEGIN: early CSL µs alarm before tasklets */
void nrf54ProcessMainLoop(otInstance *aInstance)
{
#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
    if (nrf5AlarmIsPending())
    {
        nrf5AlarmProcess(aInstance);
    }
#endif

    otTaskletsProcess(aInstance);
    otSysProcessDrivers(aInstance);
}
/* CSL-F4.1-END */
