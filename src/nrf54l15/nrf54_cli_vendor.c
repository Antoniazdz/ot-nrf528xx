/*
 * Copyright (c) 2026, The OpenThread Authors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <openthread/cli.h>
#include <string.h>

#include "common/code_utils.hpp"
#include "nrf54_debug_stats_dump.h"

static void cliEmitStatsLine(void *aContext, const char *aLine)
{
    OT_UNUSED_VARIABLE(aContext);
    otCliOutputFormat("%s\r\n", aLine);
}

static otError processNrf54Stats(void *aContext, uint8_t aArgsLength, char *aArgs[])
{
    OT_UNUSED_VARIABLE(aContext);

    if (aArgsLength >= 1 && strcmp(aArgs[0], "clear") == 0)
    {
        nrf54DebugStatsClear();
        otCliOutputFormat("Done\r\n");
        return OT_ERROR_NONE;
    }

    if (aArgsLength >= 1 && strcmp(aArgs[0], "raw") == 0)
    {
        nrf54DebugStatsDumpRawEmit(cliEmitStatsLine, NULL);
        otCliOutputFormat("Done\r\n");
        return OT_ERROR_NONE;
    }

    nrf54DebugStatsDumpSummaryEmit(cliEmitStatsLine, NULL);
    otCliOutputFormat("Done\r\n");
    return OT_ERROR_NONE;
}

static const otCliCommand sNrf54CliCommands[] = {
    {"nrf54stats", processNrf54Stats},
};

void otCliVendorSetUserCommands(void)
{
    IgnoreError(otCliSetUserCommands(sNrf54CliCommands, OT_ARRAY_LENGTH(sNrf54CliCommands), NULL));
}
