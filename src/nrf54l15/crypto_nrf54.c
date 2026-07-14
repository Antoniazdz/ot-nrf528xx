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
 *   POC crypto stub for nRF54L15 bare-metal RCP.
 *
 *   nRF52 hardware ECB (nrf_ecb_*) is unavailable on nRF54. Thread crypto uses
 *   mbedTLS; replace with CRACEN or software AES when platform AES is needed.
 */

#include <openthread/platform/crypto.h>

#include <openthread-core-config.h>
#include <openthread/config.h>

#include "platform-nrf5.h"

void nrf5CryptoInit(void)
{
    // Intentionally empty — keeps platform crypto symbols linked for ot-rcp.
}

void nrf5CryptoDeinit(void)
{
}

otError otPlatCryptoAesInit(otCryptoContext *aContext)
{
    OT_UNUSED_VARIABLE(aContext);

    return OT_ERROR_NONE;
}

otError otPlatCryptoAesSetKey(otCryptoContext *aContext, const otCryptoKey *aKey)
{
    OT_UNUSED_VARIABLE(aContext);
    OT_UNUSED_VARIABLE(aKey);

    return OT_ERROR_NONE;
}

otError otPlatCryptoAesEncrypt(otCryptoContext *aContext, const uint8_t *aInput, uint8_t *aOutput)
{
    OT_UNUSED_VARIABLE(aContext);
    OT_UNUSED_VARIABLE(aInput);
    OT_UNUSED_VARIABLE(aOutput);

    return OT_ERROR_NOT_IMPLEMENTED;
}

otError otPlatCryptoAesFree(otCryptoContext *aContext)
{
    OT_UNUSED_VARIABLE(aContext);

    return OT_ERROR_NONE;
}
