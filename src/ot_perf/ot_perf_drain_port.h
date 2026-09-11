/*
 *  Copyright (c) 2026, The OpenThread Authors.
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Platform hook for perf client radio-queue drain (bare-metal deferred callbacks).
 */

#ifndef OT_PERF_DRAIN_PORT_H_
#define OT_PERF_DRAIN_PORT_H_

#include <stdbool.h>

/* True while the platform still has deferred otPlatRadioTxDone/ReceiveDone work. */
bool otPerfPlatformRadioPending(void);

#endif // OT_PERF_DRAIN_PORT_H_
