/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "ot_perf_drain_port.h"

#include "platform-nrf5.h"

bool otPerfPlatformRadioPending(void) { return nrf5RadioHasPendingCallbacks(); }
