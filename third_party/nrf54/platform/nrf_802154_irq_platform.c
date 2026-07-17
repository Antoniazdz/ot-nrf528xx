/*
 * Copyright (c) 2026, The OpenThread Authors.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Bare-metal NVIC glue for the nRF 802.15.4 driver IRQ abstraction layer.
 */

#include "platform/nrf_802154_irq.h"

#include <stdbool.h>
#include <stdint.h>

#include <nrfx.h>

/* Declared in driver when NRF_802154_INTERNAL_RADIO_IRQ_HANDLING=0 (default on nRF54 SL). */
void nrf_802154_radio_irq_handler(void);

#define NRF_802154_IRQ_DISPATCH_TABLE_SIZE 8U

typedef struct
{
    int32_t          irqn;
    nrf_802154_isr_t isr;
} nrf_802154_irq_dispatch_entry_t;

static nrf_802154_irq_dispatch_entry_t m_irq_dispatch[NRF_802154_IRQ_DISPATCH_TABLE_SIZE];
static uint8_t                         m_irq_dispatch_count;

static nrf_802154_isr_t irq_dispatch_get(int32_t irqn)
{
    for (uint8_t i = 0; i < m_irq_dispatch_count; i++)
    {
        if (m_irq_dispatch[i].irqn == irqn)
        {
            return m_irq_dispatch[i].isr;
        }
    }

    return NULL;
}

static void irq_dispatch_register(int32_t irqn, nrf_802154_isr_t isr)
{
    for (uint8_t i = 0; i < m_irq_dispatch_count; i++)
    {
        if (m_irq_dispatch[i].irqn == irqn)
        {
            m_irq_dispatch[i].isr = isr;
            return;
        }
    }

    if (m_irq_dispatch_count < NRF_802154_IRQ_DISPATCH_TABLE_SIZE)
    {
        m_irq_dispatch[m_irq_dispatch_count].irqn = irqn;
        m_irq_dispatch[m_irq_dispatch_count].isr    = isr;
        m_irq_dispatch_count++;
    }
}

void nrf_802154_irq_init(uint32_t irqn, int32_t prio, nrf_802154_isr_t isr)
{
    NVIC_SetPriority((IRQn_Type)irqn, (uint32_t)prio);

    if (isr != NULL)
    {
        irq_dispatch_register((int32_t)irqn, isr);
        
    }
}

void nrf_802154_irq_enable(uint32_t irqn)
{
    NVIC_EnableIRQ((IRQn_Type)irqn);
}

void nrf_802154_irq_disable(uint32_t irqn)
{
    NVIC_DisableIRQ((IRQn_Type)irqn);
}

void nrf_802154_irq_set_pending(uint32_t irqn)
{
    NVIC_SetPendingIRQ((IRQn_Type)irqn);
}

void nrf_802154_irq_clear_pending(uint32_t irqn)
{
    NVIC_ClearPendingIRQ((IRQn_Type)irqn);
}

bool nrf_802154_irq_is_enabled(uint32_t irqn)
{
    return (NVIC->ISER[irqn >> 5UL] & (1UL << (irqn & 0x1FUL))) != 0U;
}

uint32_t nrf_802154_irq_priority_get(uint32_t irqn)
{
    return NVIC_GetPriority((IRQn_Type)irqn);
}

void AAR00_CCM00_IRQHandler(void)
{
    nrf_802154_isr_t isr = irq_dispatch_get((int32_t)nrfx_get_irq_number(NRF_CCM00));

    if (isr != NULL)
    {
        isr();
    }
}

void RADIO_0_IRQHandler(void)
{
    nrf_802154_radio_irq_handler();
}
