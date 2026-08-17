/*
 *  Copyright (c) 2017, The OpenThread Authors.
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

 #include <stdbool.h>
 #include <stdint.h>
 #include <string.h>
 #include <nrfx.h>

 #include <nrfx_rramc.h>
 #include "platform-nrf5.h"

 #define FLASH_PAGE_SIZE       4096
 #define RRAM_WRITE_LINE_SIZE  16   /* 128-bit wordline w RRAM */

 static bool sInitialized;
void nrf5FlashInit(void)
{
    if (!sInitialized)
    {
        static const nrfx_rramc_config_t kConfig = NRFX_RRAMC_DEFAULT_CONFIG(0);
        nrfx_rramc_init(&kConfig, NULL);
        nrfx_rramc_write_enable_set(true, 0);  /* write mode ON, bez bufora */
        sInitialized = true;
    }
}

static void rramcWaitReady(void)
{
    while (!nrfx_rramc_ready_check())
    {
    }
}
static bool lineIsErased(uint32_t aAddress)
{
    const uint32_t *words = (const uint32_t *)aAddress;

    return words[0] == 0xFFFFFFFFUL && words[1] == 0xFFFFFFFFUL &&
           words[2] == 0xFFFFFFFFUL && words[3] == 0xFFFFFFFFUL;
}

otError nrf5FlashSwapErase(uint32_t aAddress, uint32_t aSize)
{
    nrf5FlashInit();

    for (uint32_t offset = 0; offset < aSize; offset += RRAM_WRITE_LINE_SIZE)
    {
        uint32_t line = aAddress + offset;

        if (!lineIsErased(line))
        {
            /* write mode już włączony — store do RRAM działa jak zwykły zapis */
            memset((void *)line, 0xFF, RRAM_WRITE_LINE_SIZE);
        }
    }

    rramcWaitReady();
    return OT_ERROR_NONE;
}

otError nrf5FlashPageErase(uint32_t aAddress)
{
    return nrf5FlashSwapErase(aAddress, FLASH_PAGE_SIZE);
}

otError nrf5FlashWrite(uint32_t aAddress, const uint8_t *aData, uint32_t aSize)
{
    nrf5FlashInit();

    /* RRAM: overwrite bez wcześniejszego erase w tym miejscu */
    nrfx_rramc_bytes_write(aAddress, aData, aSize);

    rramcWaitReady();
    return OT_ERROR_NONE;
}
bool nrf5FlashIsBusy(void)
{
    return !nrfx_rramc_ready_check();
}