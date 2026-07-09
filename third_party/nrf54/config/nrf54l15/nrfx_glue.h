/*
 * Copyright (c) 2017 - 2026, Nordic Semiconductor ASA
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Bare-metal nrfx glue for ot-nrf54xx (nRF54L15 RCP POC).
 * Skeleton: third_party/nrf54/nordic/nrfx/templates/nrfx_glue.h
 * IRQ / critical section pattern: third_party/NordicSemiconductor/dependencies/nrfx_glue.h
 * Atomic / CLZ / cache stubs: NCS zephyr/modules/hal_nordic/nrfx/nrfx_glue.h (CMSIS, no Zephyr)
 */

#ifndef NRFX_GLUE_H__
#define NRFX_GLUE_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup nrfx_glue nrfx_glue.h
 * @{
 * @ingroup nrfx
 *
 * @brief Host integration macros for standalone nrfx on nRF54L15.
 */

//------------------------------------------------------------------------------

/** @brief Runtime assertion (disabled in POC bring-up). */
#define NRFX_ASSERT(expression) ((void)(expression))

/** @brief Compile-time assertion. */
#define NRFX_STATIC_ASSERT(expression) _Static_assert(expression, "nrfx")

//------------------------------------------------------------------------------

#ifndef SOFTDEVICE_PRESENT
#define INTERRUPT_PRIORITY_IS_VALID(priority) ((priority) < (1UL << __NVIC_PRIO_BITS))
#else
#define INTERRUPT_PRIORITY_IS_VALID(priority) \
    ((((priority) > 1) && ((priority) < 4)) || (((priority) > 4) && ((priority) < 8)))
#endif

#define NRFX_IRQ_PRIORITY_SET(irq_number, priority) \
    _nrfx_glue_irq_priority_set((irq_number), (priority))

static inline void _nrfx_glue_irq_priority_set(IRQn_Type irq_number, uint8_t priority)
{
    NRFX_ASSERT(INTERRUPT_PRIORITY_IS_VALID(priority));
    NVIC_SetPriority(irq_number, priority);
}

#define NRFX_IRQ_ENABLE(irq_number) _nrfx_glue_irq_enable(irq_number)

static inline void _nrfx_glue_irq_enable(IRQn_Type irq_number)
{
    NVIC_EnableIRQ(irq_number);
}

#define NRFX_IRQ_IS_ENABLED(irq_number) _nrfx_glue_irq_is_enabled(irq_number)

static inline bool _nrfx_glue_irq_is_enabled(IRQn_Type irq_number)
{
    return (NVIC->ISER[((uint32_t)irq_number) >> 5UL] &
            (1UL << (((uint32_t)irq_number) & 0x1FUL))) != 0UL;
}

#define NRFX_IRQ_DISABLE(irq_number) _nrfx_glue_irq_disable(irq_number)

static inline void _nrfx_glue_irq_disable(IRQn_Type irq_number)
{
    NVIC_DisableIRQ(irq_number);
}

#define NRFX_IRQ_PENDING_SET(irq_number) _nrfx_glue_irq_pending_set(irq_number)

static inline void _nrfx_glue_irq_pending_set(IRQn_Type irq_number)
{
    NVIC_SetPendingIRQ(irq_number);
}

#define NRFX_IRQ_PENDING_CLEAR(irq_number) _nrfx_glue_irq_pending_clear(irq_number)

static inline void _nrfx_glue_irq_pending_clear(IRQn_Type irq_number)
{
    NVIC_ClearPendingIRQ(irq_number);
}

#define NRFX_IRQ_IS_PENDING(irq_number) _nrfx_glue_irq_is_pending(irq_number)

static inline bool _nrfx_glue_irq_is_pending(IRQn_Type irq_number)
{
    return NVIC_GetPendingIRQ(irq_number) == 1;
}

/** @brief Enter critical section (PRIMASK). */
#define NRFX_CRITICAL_SECTION_ENTER()            \
    {                                            \
        uint32_t _nrfx_cs_primask = __get_PRIMASK(); \
        __disable_irq()

/** @brief Exit critical section (PRIMASK). */
#define NRFX_CRITICAL_SECTION_EXIT()             \
        __set_PRIMASK(_nrfx_cs_primask);         \
    }

//------------------------------------------------------------------------------

#define NRFX_COREDEP_DELAY_DWT_BASED 0

extern uint32_t SystemCoreClock;

static inline void _nrfx_glue_delay_us(uint32_t time_us)
{
    if (time_us == 0U)
    {
        return;
    }

    uint32_t cycles = time_us * (SystemCoreClock / 1000000U);

    for (volatile uint32_t i = 0; i < (cycles / 3U); i++)
    {
        __NOP();
    }
}

#define NRFX_DELAY_US(us_time) _nrfx_glue_delay_us(us_time)

//------------------------------------------------------------------------------

typedef volatile uint32_t nrfx_atomic_t;

static inline uint32_t _nrfx_glue_atomic_fetch_store(nrfx_atomic_t * p_data, uint32_t value)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    uint32_t old_value = *p_data;
    *p_data            = value;
    __set_PRIMASK(primask);

    return old_value;
}

static inline uint32_t _nrfx_glue_atomic_fetch_or(nrfx_atomic_t * p_data, uint32_t value)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    uint32_t old_value = *p_data;
    *p_data            = old_value | value;
    __set_PRIMASK(primask);

    return old_value;
}

static inline uint32_t _nrfx_glue_atomic_fetch_and(nrfx_atomic_t * p_data, uint32_t value)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    uint32_t old_value = *p_data;
    *p_data            = old_value & value;
    __set_PRIMASK(primask);

    return old_value;
}

static inline uint32_t _nrfx_glue_atomic_fetch_xor(nrfx_atomic_t * p_data, uint32_t value)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    uint32_t old_value = *p_data;
    *p_data            = old_value ^ value;
    __set_PRIMASK(primask);

    return old_value;
}

static inline uint32_t _nrfx_glue_atomic_fetch_add(nrfx_atomic_t * p_data, uint32_t value)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    uint32_t old_value = *p_data;
    *p_data            = old_value + value;
    __set_PRIMASK(primask);

    return old_value;
}

static inline uint32_t _nrfx_glue_atomic_fetch_sub(nrfx_atomic_t * p_data, uint32_t value)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    uint32_t old_value = *p_data;
    *p_data            = old_value - value;
    __set_PRIMASK(primask);

    return old_value;
}

static inline bool _nrfx_glue_atomic_cas(nrfx_atomic_t * p_data,
                                         uint32_t        old_value,
                                         uint32_t        new_value)
{
    uint32_t primask = __get_PRIMASK();
    bool     status  = false;

    __disable_irq();
    if (*p_data == old_value)
    {
        *p_data = new_value;
        status  = true;
    }
    __set_PRIMASK(primask);

    return status;
}

#define NRFX_ATOMIC_FETCH_STORE(p_data, value) _nrfx_glue_atomic_fetch_store((p_data), (value))
#define NRFX_ATOMIC_FETCH_OR(p_data, value) _nrfx_glue_atomic_fetch_or((p_data), (value))
#define NRFX_ATOMIC_FETCH_AND(p_data, value) _nrfx_glue_atomic_fetch_and((p_data), (value))
#define NRFX_ATOMIC_FETCH_XOR(p_data, value) _nrfx_glue_atomic_fetch_xor((p_data), (value))
#define NRFX_ATOMIC_FETCH_ADD(p_data, value) _nrfx_glue_atomic_fetch_add((p_data), (value))
#define NRFX_ATOMIC_FETCH_SUB(p_data, value) _nrfx_glue_atomic_fetch_sub((p_data), (value))
#define NRFX_ATOMIC_CAS(p_data, old_value, new_value) \
    _nrfx_glue_atomic_cas((p_data), (old_value), (new_value))

#define NRFX_CLZ(value) __builtin_clz(value)
#define NRFX_CTZ(value) __builtin_ctz(value)

//------------------------------------------------------------------------------

#define NRFX_EVENT_READBACK_ENABLED 1

//------------------------------------------------------------------------------

#define NRFY_CACHE_WB(p_buffer, size)    \
    do                                   \
    {                                    \
        (void)(p_buffer);              \
        (void)(size);                    \
    } while (0)

#define NRFY_CACHE_INV(p_buffer, size)   \
    do                                   \
    {                                    \
        (void)(p_buffer);                \
        (void)(size);                    \
    } while (0)

#define NRFY_CACHE_WBINV(p_buffer, size) \
    do                                   \
    {                                    \
        (void)(p_buffer);                \
        (void)(size);                    \
    } while (0)

//------------------------------------------------------------------------------

#define NRFX_DPPI_CHANNELS_USED 0
#define NRFX_DPPI_GROUPS_USED 0
#define NRFX_PPI_CHANNELS_USED 0
#define NRFX_PPI_GROUPS_USED 0
#define NRFX_GPIOTE_CHANNELS_USED 0
#define NRFX_EGUS_USED 0
#define NRFX_TIMERS_USED 0

/** @} */

#ifdef __cplusplus
}
#endif

#endif // NRFX_GLUE_H__
