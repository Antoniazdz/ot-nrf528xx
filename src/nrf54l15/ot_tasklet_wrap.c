/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  CSL-F4.1: vanilla OT main.c calls otTaskletsProcess before otSysProcessDrivers.
 *  Link with -Wl,--wrap=otTaskletsProcess so the µs alarm runs before tasklets.
 */

#include <openthread-core-config.h>
#include <openthread/instance.h>
#include <openthread/tasklet.h>

#include "platform-nrf5.h"

#ifdef NRF54_DEBUG_STATS
#include "nrf54_debug_stats.h"
#endif

extern void __real_otTaskletsProcess(otInstance *aInstance);

void __wrap_otTaskletsProcess(otInstance *aInstance)
{
#if OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE
    if (nrf5AlarmIsPending())
    {
#ifdef NRF54_DEBUG_STATS
        g_nrf54_debug_stats.csl_alarm_process_early++;
#endif
        nrf5AlarmProcess(aInstance);
    }
#endif

    __real_otTaskletsProcess(aInstance);
}
