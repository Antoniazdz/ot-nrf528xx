/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Optional perf timing hooks. Weak no-ops live in perf_timing_stubs.c; ot-perf
 *  overrides them when linked.
 */

#ifndef PERF_TIMING_PORT_H_
#define PERF_TIMING_PORT_H_

#include <stdbool.h>
#include <stdint.h>

bool otPerfTimingIsActive(void);
void otPerfTimingDriverPassBegin(void);
void otPerfTimingAddTransportUs(uint32_t aMicroseconds);
void otPerfTimingAddPerfProcessUs(uint32_t aMicroseconds);
void otPerfTimingAddRadioPreUs(uint32_t aMicroseconds);
void otPerfTimingAddRadioPostUs(uint32_t aMicroseconds);
void otPerfTimingDriverPassEnd(void);

#endif // PERF_TIMING_PORT_H_
