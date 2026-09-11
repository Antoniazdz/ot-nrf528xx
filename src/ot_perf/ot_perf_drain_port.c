/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "ot_perf_drain_port.h"

#include <openthread/platform/toolchain.h>

OT_TOOL_WEAK bool otPerfPlatformRadioPending(void) { return false; }
