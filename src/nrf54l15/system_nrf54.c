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
#include "platform-fem.h"
#include "platform-nrf5-transport.h"
#include "platform-nrf5.h"

#include <nrfx.h>
/* CSL-F4.1-BEGIN: tasklets + early alarm in main loop */
#include <openthread/tasklet.h>
/* CSL-F4.1-END */

#if !OPENTHREAD_CONFIG_ENABLE_BUILTIN_MBEDTLS_MANAGEMENT && PLATFORM_OPENTHREAD_VANILLA

#include <mbedtls/platform.h>
#include <mbedtls/threading.h>

#include <openthread/heap.h>

#endif

extern bool gPlatformPseudoResetWasRequested;

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

void otSysProcessDrivers(otInstance *aInstance)
{
    /* CSL-F4.1-BEGIN: alarm before radio (was last in driver pass) */
    nrf5AlarmProcess(aInstance);
    /* CSL-F4.1-END */
    nrf5RadioProcess(aInstance);
    nrf5TransportProcess();
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
