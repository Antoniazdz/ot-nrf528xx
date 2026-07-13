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
 *   POC entropy stub for nRF54L15 bare-metal RCP.
 *
 *   Uses libc PRNG only — not a TRNG. Replace with CRACEN (nrfx_cracen) before
 *   any production or crypto-sensitive use.
 */

#include <openthread/platform/entropy.h>

#include <openthread-core-config.h>
#include <openthread/config.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include <utils/code_utils.h>

#include "platform-nrf5.h"

static volatile bool sEntropyGetEntered;

void nrf5RandomInit(void)
{
    srand(0xDEADBEEF);
}

void nrf5RandomDeinit(void)
{
}

otError otPlatEntropyGet(uint8_t *aOutput, uint16_t aOutputLength)
{
    otError error = OT_ERROR_NONE;

    assert(!sEntropyGetEntered);
    sEntropyGetEntered = true;

    otEXPECT_ACTION(aOutput != NULL && aOutputLength > 0, error = OT_ERROR_INVALID_ARGS);

    for (uint16_t i = 0; i < aOutputLength; i++)
    {
        aOutput[i] = (uint8_t)rand();
    }

exit:
    sEntropyGetEntered = false;

    return error;
}
