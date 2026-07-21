/*
 * Copyright (c) 2026, The OpenThread Authors.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Bare-metal nRF54L15 GRTC platform layer for nrf_802154 SL lptimer.
 *
 * Phase 1 (baseline): time, init/deinit, critical section — matches lptimer_stub.c.
 * Phase 2 (CC2): callback compare channel — set NRF54_LPTIMER_CC2_STUB_BISECT to 0.
 * Phase 3 (CC8): hw_task + DPPI — set NRF54_LPTIMER_CC2_ONLY_BISECT to 0.
 *
 * CC3 sync is not used on nRF54 GRTC (see NCS lptimer_grtc.c); sync_* APIs are stubs.
 *
 * Reference: NCS nrf_802154_platform_sl_lptimer_grtc.c + lptimer_grtc_hw_task.c
 */

#include "platform/nrf_802154_platform_sl_lptimer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <nrfx_grtc.h>
#include <haly/nrfy_grtc.h>

/*
 * Phase 3 bisect: CC2 on, CC8/DPPI off when 1. Set to 0 to enable hw_task (CC8 + grtc_hw_task.c).
 */
#ifndef NRF54_LPTIMER_CC2_ONLY_BISECT
#define NRF54_LPTIMER_CC2_ONLY_BISECT 1
#endif

#include "nrf54_debug_stats.h"

/*
 * Phase 1/2 bisect: no CC2 alloc, schedule_at/disable no-op (stub baseline).
 * Set to 0 to enable CC2 callback timer (required for ping).
 */
#ifndef NRF54_LPTIMER_CC2_STUB_BISECT
#define NRF54_LPTIMER_CC2_STUB_BISECT 0
#endif

#if !NRF54_LPTIMER_CC2_ONLY_BISECT && NRF54_LPTIMER_CC2_STUB_BISECT
#error CC8 (NRF54_LPTIMER_CC2_ONLY_BISECT=0) requires CC2 (NRF54_LPTIMER_CC2_STUB_BISECT=0)
#endif

#if !NRF54_LPTIMER_CC2_STUB_BISECT
#include "nrf_802154_sl_utils.h"
#endif

#if !NRF54_LPTIMER_CC2_ONLY_BISECT
#include "nrf_802154_platform_sl_lptimer_grtc_hw_task.h"
#include "nrf_802154_sl_atomics.h"
#endif

#ifndef OT_GRTC_US_PER_TICK
#define OT_GRTC_US_PER_TICK 1ULL
#endif
#ifndef OT_GRTC_CC_RADIO_TIMER
#define OT_GRTC_CC_RADIO_TIMER 2
#endif
#ifndef OT_GRTC_CC_RADIO_HW_TASK
#define OT_GRTC_CC_RADIO_HW_TASK 8
#endif

/* Shared with stub baseline (Phase 1). */
static uint32_t m_critical_section_cnt;

/* -------------------------------------------------------------------------- */
/* Phase 2 — CC2 callback compare channel                                       */
/* -------------------------------------------------------------------------- */

#if !NRF54_LPTIMER_CC2_STUB_BISECT

static volatile bool       m_enabled;
static bool                m_compare_int_was_enabled;
static uint8_t             m_callbacks_channel;
static nrfx_grtc_channel_t m_callbacks_channel_data;

static inline bool is_lptimer_enabled(void)
{
    return m_enabled;
}

static bool compare_int_lock(uint8_t aChannel)
{
    bool was_enabled = nrfx_grtc_syscounter_cc_int_enable_check(aChannel);

    (void)nrfx_grtc_syscounter_cc_int_disable(aChannel);

    return was_enabled;
}

static void compare_int_unlock(uint8_t aChannel, bool aWasEnabled)
{
    if (aWasEnabled)
    {
        (void)nrfx_grtc_syscounter_cc_int_enable(aChannel);
    }
}

static void timer_compare_handler(int32_t aChannel, uint64_t aExpireTime, void *aUserData)
{
    uint64_t curr_ticks;

    (void)aUserData;
    (void)aExpireTime;

    assert((uint8_t)aChannel == m_callbacks_channel);

    if (!is_lptimer_enabled())
    {
        return;
    }

    // #region agent log
    g_nrf54_debug_stats.cc2_timer_fires++;
    // #endregion

    curr_ticks = nrfx_grtc_syscounter_get();
    nrf_802154_sl_timer_handler(curr_ticks);
}

static void compare_schedule(uint8_t aChannel, nrfx_grtc_channel_t *aChannelData, uint64_t aFireLpticks)
{
    /*
     * Match NCS lptimer_grtc.c / pre-bisect git: disable → set → always enable CC int.
     * Do not use compare_int_unlock() here — if the channel was not yet enabled,
     * unlock leaves CC2 IRQ off and sl_timer_handler never runs under thread start.
     */
    (void)nrfx_grtc_syscounter_cc_int_disable(aChannel);
    (void)nrfx_grtc_syscounter_cc_absolute_set(aChannelData, aFireLpticks, true);
    (void)nrfx_grtc_syscounter_cc_int_enable(aChannel);
}

#endif // !NRF54_LPTIMER_CC2_STUB_BISECT

/* -------------------------------------------------------------------------- */
/* Phase 3 — CC8 hardware task (DPPI)                                           */
/* -------------------------------------------------------------------------- */

#if !NRF54_LPTIMER_CC2_ONLY_BISECT

typedef uint8_t hw_task_state_t;

#define HW_TASK_STATE_IDLE       0U
#define HW_TASK_STATE_SETTING_UP 1U
#define HW_TASK_STATE_READY      2U
#define HW_TASK_STATE_CLEANING   3U
#define HW_TASK_STATE_UPDATING   4U

#define HW_TASK_MINIMUM_MARGIN_LPTICKS 1ULL

static volatile hw_task_state_t m_hw_task_state;
static uint64_t                 m_hw_task_fire_lpticks;
static uint8_t                  m_hw_task_channel;
static nrfx_grtc_channel_t      m_hw_task_channel_data;

static bool hw_task_state_set(hw_task_state_t aExpectedState, hw_task_state_t aNewState)
{
    return nrf_802154_sl_atomic_cas_u8((uint8_t *)&m_hw_task_state, &aExpectedState, aNewState);
}

static void hw_task_schedule(uint64_t aFireLpticks)
{
    /*
     * CC8 has no CPU handler — only the compare EVENT must reach DPPI/RADIO.
     * Do not use compare_int_lock/unlock (CC2 IRQ pattern); cc_channel_prepare()
     * disables the compare event and syscounter_cc_absolute_set() does not re-enable it.
     */
    (void)nrfx_grtc_syscounter_cc_int_disable(m_hw_task_channel);
    (void)nrfx_grtc_syscounter_cc_absolute_set(&m_hw_task_channel_data, aFireLpticks, false);
    nrfy_grtc_sys_counter_compare_event_enable(NRF_GRTC, m_hw_task_channel);
}

static void hw_task_abort(void)
{
    bool irq_key;

    irq_key = compare_int_lock(m_hw_task_channel);
    (void)nrfx_grtc_syscounter_cc_disable(m_hw_task_channel);
    compare_int_unlock(m_hw_task_channel, irq_key);
}

static bool hw_task_compare_evt_check(void)
{
    uint32_t event_address = nrfx_grtc_event_compare_address_get(m_hw_task_channel);

    return (*(volatile uint32_t *)event_address) != 0U;
}

#endif // !NRF54_LPTIMER_CC2_ONLY_BISECT

/* -------------------------------------------------------------------------- */
/* Platform API                                                                 */
/* -------------------------------------------------------------------------- */

void nrf_802154_platform_sl_lp_timer_init(void)
{
    m_critical_section_cnt = 0;

#if !NRF54_LPTIMER_CC2_STUB_BISECT
    m_compare_int_was_enabled = false;
    m_enabled                 = false;

    assert(nrfx_grtc_channel_alloc(&m_callbacks_channel) == 0);
    assert(m_callbacks_channel == OT_GRTC_CC_RADIO_TIMER);

    m_callbacks_channel_data.channel   = m_callbacks_channel;
    m_callbacks_channel_data.handler   = timer_compare_handler;
    m_callbacks_channel_data.p_context = NULL;
#endif

#if !NRF54_LPTIMER_CC2_ONLY_BISECT
    m_hw_task_state = HW_TASK_STATE_IDLE;

    assert(nrfx_grtc_channel_alloc(&m_hw_task_channel) == 0);
    assert(m_hw_task_channel == OT_GRTC_CC_RADIO_HW_TASK);

    m_hw_task_channel_data.channel   = m_hw_task_channel;
    m_hw_task_channel_data.handler   = NULL;
    m_hw_task_channel_data.p_context = NULL;

    assert(nrf_802154_platform_sl_lptimer_hw_task_cross_domain_connections_setup(m_hw_task_channel) == 0);
#endif

    assert(nrfx_grtc_init_check());
}

void nrf_802154_platform_sl_lp_timer_deinit(void)
{
#if !NRF54_LPTIMER_CC2_ONLY_BISECT
    if (m_hw_task_state == HW_TASK_STATE_READY)
    {
        (void)nrf_802154_platform_sl_lptimer_hw_task_cleanup();
    }

    nrf_802154_platform_sl_lptimer_hw_task_cross_domain_connections_clear();
    hw_task_abort();
    (void)nrfx_grtc_channel_free(m_hw_task_channel);
#endif

#if !NRF54_LPTIMER_CC2_STUB_BISECT
    (void)compare_int_lock(m_callbacks_channel);
    (void)nrfx_grtc_syscounter_cc_disable(m_callbacks_channel);
    (void)nrfx_grtc_channel_free(m_callbacks_channel);
    m_enabled = false;
#endif

    m_critical_section_cnt = 0;
}

uint64_t nrf_802154_platform_sl_lptimer_current_lpticks_get(void)
{
    if (!nrfx_grtc_init_check())
    {
        return 0;
    }

    return nrfx_grtc_syscounter_get();
}

uint64_t nrf_802154_platform_sl_lptimer_us_to_lpticks_convert(uint64_t us, bool round_up)
{
    uint64_t ticks = us / OT_GRTC_US_PER_TICK;

    if (round_up && (ticks * OT_GRTC_US_PER_TICK < us))
    {
        ticks++;
    }

    return ticks;
}

uint64_t nrf_802154_platform_sl_lptimer_lpticks_to_us_convert(uint64_t lpticks)
{
    return lpticks * OT_GRTC_US_PER_TICK;
}

void nrf_802154_platform_sl_lptimer_schedule_at(uint64_t fire_lpticks)
{
#if NRF54_LPTIMER_CC2_STUB_BISECT
    (void)fire_lpticks;
#else
    m_enabled = true;
    compare_schedule(m_callbacks_channel, &m_callbacks_channel_data, fire_lpticks);
#endif
}

void nrf_802154_platform_sl_lptimer_disable(void)
{
#if !NRF54_LPTIMER_CC2_STUB_BISECT
    bool irq_key;

    m_enabled = false;

    irq_key = compare_int_lock(m_callbacks_channel);
    (void)nrfx_grtc_syscounter_cc_disable(m_callbacks_channel);
    compare_int_unlock(m_callbacks_channel, irq_key);
#endif
}

void nrf_802154_platform_sl_lptimer_critical_section_enter(void)
{
#if NRF54_LPTIMER_CC2_STUB_BISECT
    m_critical_section_cnt++;
#else
    nrf_802154_sl_mcu_critical_state_t state;

    nrf_802154_sl_mcu_critical_enter(state);

    m_critical_section_cnt++;

    if (m_critical_section_cnt == 1U)
    {
        m_compare_int_was_enabled = compare_int_lock(m_callbacks_channel);
    }

    nrf_802154_sl_mcu_critical_exit(state);
#endif
}

void nrf_802154_platform_sl_lptimer_critical_section_exit(void)
{
#if NRF54_LPTIMER_CC2_STUB_BISECT
    assert(m_critical_section_cnt > 0U);
    m_critical_section_cnt--;
#else
    nrf_802154_sl_mcu_critical_state_t state;

    nrf_802154_sl_mcu_critical_enter(state);

    assert(m_critical_section_cnt > 0U);

    if (m_critical_section_cnt == 1U)
    {
        compare_int_unlock(m_callbacks_channel, m_compare_int_was_enabled);
        m_compare_int_was_enabled = false;
    }

    m_critical_section_cnt--;

    nrf_802154_sl_mcu_critical_exit(state);
#endif
}

uint32_t nrf_802154_platform_sl_lptimer_granularity_get(void)
{
    return (uint32_t)OT_GRTC_US_PER_TICK;
}

/* CC3 sync — not used on nRF54 GRTC; stubs match lptimer_stub.c. */

void nrf_802154_platform_sl_lptimer_sync_schedule_now(void)
{
}

void nrf_802154_platform_sl_lptimer_sync_schedule_at(uint64_t fire_lpticks)
{
    (void)fire_lpticks;
}

void nrf_802154_platform_sl_lptimer_sync_abort(void)
{
}

uint32_t nrf_802154_platform_sl_lptimer_sync_event_get(void)
{
    return 0U;
}

uint64_t nrf_802154_platform_sl_lptimer_sync_lpticks_get(void)
{
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Phase 3 — hw_task API                                                        */
/* -------------------------------------------------------------------------- */

#if NRF54_LPTIMER_CC2_ONLY_BISECT

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_prepare(uint64_t fire_lpticks,
                                                                                        uint32_t ppi_channel)
{
    (void)fire_lpticks;
    (void)ppi_channel;

    return NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE;
}

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_cleanup(void)
{
    return NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE;
}

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_update_ppi(uint32_t ppi_channel)
{
    (void)ppi_channel;

    return NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE;
}

#else // NRF54_LPTIMER_CC2_ONLY_BISECT

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_prepare(uint64_t fire_lpticks,
                                                                                        uint32_t ppi_channel)
{
    uint64_t                           syscnt_now;
    bool                               done_on_time = true;
    nrf_802154_sl_mcu_critical_state_t mcu_cs_state;

    if (!hw_task_state_set(HW_TASK_STATE_IDLE, HW_TASK_STATE_SETTING_UP))
    {
        return NRF_802154_SL_LPTIMER_PLATFORM_NO_RESOURCES;
    }

    nrf_802154_platform_sl_lptimer_hw_task_local_domain_connections_setup(ppi_channel, m_hw_task_channel);

    m_hw_task_fire_lpticks = fire_lpticks;
    hw_task_schedule(fire_lpticks);

    nrf_802154_sl_mcu_critical_enter(mcu_cs_state);

    syscnt_now = nrfx_grtc_syscounter_get();

    if (syscnt_now + HW_TASK_MINIMUM_MARGIN_LPTICKS >= fire_lpticks)
    {
        nrf_802154_platform_sl_lptimer_hw_task_local_domain_connections_clear();
        hw_task_abort();
        done_on_time = false;
    }

    nrf_802154_sl_mcu_critical_exit(mcu_cs_state);

    (void)hw_task_state_set(HW_TASK_STATE_SETTING_UP, done_on_time ? HW_TASK_STATE_READY : HW_TASK_STATE_IDLE);

    // #region agent log
    if (done_on_time)
    {
        g_nrf54_debug_stats.hw_task_prepare_ok++;
    }
    else
    {
        g_nrf54_debug_stats.hw_task_prepare_fail++;
    }
    // #endregion

    return done_on_time ? NRF_802154_SL_LPTIMER_PLATFORM_SUCCESS : NRF_802154_SL_LPTIMER_PLATFORM_TOO_LATE;
}

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_cleanup(void)
{
    if (!hw_task_state_set(HW_TASK_STATE_READY, HW_TASK_STATE_CLEANING))
    {
        return NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE;
    }

    nrf_802154_platform_sl_lptimer_hw_task_local_domain_connections_clear();
    hw_task_abort();

    (void)hw_task_state_set(HW_TASK_STATE_CLEANING, HW_TASK_STATE_IDLE);

    return NRF_802154_SL_LPTIMER_PLATFORM_SUCCESS;
}

nrf_802154_sl_lptimer_platform_result_t nrf_802154_platform_sl_lptimer_hw_task_update_ppi(uint32_t ppi_channel)
{
    bool cc_triggered;

    if (!hw_task_state_set(HW_TASK_STATE_READY, HW_TASK_STATE_UPDATING))
    {
        return NRF_802154_SL_LPTIMER_PLATFORM_WRONG_STATE;
    }

    nrf_802154_platform_sl_lptimer_hw_task_local_domain_connections_setup(ppi_channel, m_hw_task_channel);

    cc_triggered = hw_task_compare_evt_check();
    if (nrfx_grtc_syscounter_get() >= m_hw_task_fire_lpticks)
    {
        cc_triggered = true;
    }

    (void)hw_task_state_set(HW_TASK_STATE_UPDATING, HW_TASK_STATE_READY);

    return cc_triggered ? NRF_802154_SL_LPTIMER_PLATFORM_TOO_LATE : NRF_802154_SL_LPTIMER_PLATFORM_SUCCESS;
}

#endif // NRF54_LPTIMER_CC2_ONLY_BISECT
