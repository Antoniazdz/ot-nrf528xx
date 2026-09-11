/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "perf_timing_port.h"

#include <openthread/platform/toolchain.h>

OT_TOOL_WEAK bool otPerfTimingIsActive(void) { return false; }

OT_TOOL_WEAK void otPerfTimingDriverPassBegin(void) {}

OT_TOOL_WEAK void otPerfTimingAddTransportUs(uint32_t aMicroseconds) { OT_UNUSED_VARIABLE(aMicroseconds); }

OT_TOOL_WEAK void otPerfTimingAddPerfProcessUs(uint32_t aMicroseconds) { OT_UNUSED_VARIABLE(aMicroseconds); }

OT_TOOL_WEAK void otPerfTimingAddRadioPreUs(uint32_t aMicroseconds) { OT_UNUSED_VARIABLE(aMicroseconds); }

OT_TOOL_WEAK void otPerfTimingAddRadioPostUs(uint32_t aMicroseconds) { OT_UNUSED_VARIABLE(aMicroseconds); }

OT_TOOL_WEAK void otPerfTimingDriverPassEnd(void) {}
