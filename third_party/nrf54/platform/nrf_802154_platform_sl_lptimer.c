/*
 * Copyright (c) 2026, The OpenThread Authors.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Bare-metal nRF54L15 GRTC platform layer for nrf_802154 SL lptimer.
 *
 * Phase 1: callback compare channel (CC2) — init, time, schedule/disable, critical section.
 *   Reference: NCS nrf_802154_platform_sl_lptimer_grtc.c (lines 51-137)
 *
 * Phase 2: sync compare channel (CC3) — HP/LP timestamper synchronization.
 *   Reference: NCS nrf_802154_platform_sl_lptimer_zephyr.c (sync_timer_handler, sync_*)
 *
 * Phase 3: hw_task + DPPI — timed RADIO tasks via GRTC compare + cross-domain GPPI.
 *   Reference: NCS nrf_802154_platform_sl_lptimer_grtc.c + lptimer_grtc_hw_task.c
 */

#include "platform/nrf_802154_platform_sl_lptimer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <nrfx_grtc.h>

#include "nrf_802154_platform_sl_lptimer_grtc_hw_task.h"
#include "nrf_802154_sl_atomics.h"
#include "nrf_802154_sl_utils.h"

/* GRTC channel budget — keep in sync with src/nrf54l15/platform-config.h */
#ifndef OT_GRTC_US_PER_TICK
#define OT_GRTC_US_PER_TICK 1ULL
#endif
#ifndef OT_GRTC_CC_RADIO_TIMER
#define OT_GRTC_CC_RADIO_TIMER 2
#endif
#ifndef OT_GRTC_CC_RADIO_SYNC
#define OT_GRTC_CC_RADIO_SYNC 3
#endif
#ifndef OT_GRTC_CC_RADIO_HW_TASK
#define OT_GRTC_CC_RADIO_HW_TASK 8
#endif

/* -------------------------------------------------------------------------- */
/* Phase 1 — callback compare channel (OT_GRTC_CC_RADIO_TIMER / CC2)          */
/* -------------------------------------------------------------------------- */

static volatile bool          m_enabled;
static bool                   m_compare_int_was_enabled;
static uint32_t               m_critical_section_cnt;
static uint8_t                m_callbacks_channel;
static nrfx_grtc_channel_t    m_callbacks_channel_data;

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
        /* Late interrupt after disable — ignore (NCS lptimer_grtc.c). */
        return;
    }

    curr_ticks = nrfx_grtc_syscounter_get();
    nrf_802154_sl_timer_handler(curr_ticks);
}

static void compare_schedule(uint8_t aChannel, nrfx_grtc_channel_t *aChannelData, uint64_t aFireLpticks)
{
    (void)nrfx_grtc_syscounter_cc_int_disable(aChannel);
    (void)nrfx_grtc_syscounter_cc_absolute_set(aChannelData, aFireLpticks, true);
    (void)nrfx_grtc_syscounter_cc_int_enable(aChannel);
}

/* -------------------------------------------------------------------------- */
/* Phase 2 — sync compare channel (OT_GRTC_CC_RADIO_SYNC / CC3)               */
/* -------------------------------------------------------------------------- */

#define SYNC_SCHEDULE_NOW_MARGIN_LPTICKS 3ULL

static uint64_t            m_sync_target_lpticks;
static bool                m_sync_compare_int_was_enabled;
static uint8_t             m_sync_channel;
static nrfx_grtc_channel_t m_sync_channel_data;

static void sync_compare_handler(int32_t aChannel, uint64_t aExpireTime, void *aUserData)
{
    (void)aUserData;

    assert((uint8_t)aChannel == m_sync_channel);

    /* Expire time may differ from the requested target (NCS lptimer_zephyr.c). */
    m_sync_target_lpticks = aExpireTime;

    nrf_802154_sl_timestamper_synchronized();
}

static void sync_timer_start_at(uint64_t aFireLpticks)
{
    nrf_802154_sl_mcu_critical_state_t state;

    nrf_802154_sl_mcu_critical_enter(state);

    m_sync_target_lpticks = aFireLpticks;

    nrf_802154_sl_mcu_critical_exit(state);

    compare_schedule(m_sync_channel, &m_sync_channel_data, aFireLpticks);
}

/* -------------------------------------------------------------------------- */
/* Phase 3 — hardware task compare channel (OT_GRTC_CC_RADIO_HW_TASK)           */
/* -------------------------------------------------------------------------- */

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
    (void)nrfx_grtc_syscounter_cc_int_disable(m_hw_task_channel);
    (void)nrfx_grtc_syscounter_cc_absolute_set(&m_hw_task_channel_data, aFireLpticks, true);
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

void nrf_802154_platform_sl_lp_timer_init(void)
{
    m_critical_section_cnt           = 0;
    m_compare_int_was_enabled      = false;
    m_sync_compare_int_was_enabled = false;
    m_enabled                        = false;
    m_hw_task_state                  = HW_TASK_STATE_IDLE;

    assert(nrfx_grtc_init_check());

    assert(nrfx_grtc_channel_alloc(&m_callbacks_channel) == 0);
    assert(m_callbacks_channel == OT_GRTC_CC_RADIO_TIMER);

    m_callbacks_channel_data.channel   = m_callbacks_channel;
    m_callbacks_channel_data.handler   = timer_compare_handler;
    m_callbacks_channel_data.p_context = NULL;

    assert(nrfx_grtc_channel_alloc(&m_sync_channel) == 0);
    assert(m_sync_channel == OT_GRTC_CC_RADIO_SYNC);

    m_sync_channel_data.channel   = m_sync_channel;
    m_sync_channel_data.handler   = sync_compare_handler;
    m_sync_channel_data.p_context = NULL;

    assert(nrfx_grtc_channel_alloc(&m_hw_task_channel) == 0);
    assert(m_hw_task_channel == OT_GRTC_CC_RADIO_HW_TASK);

    m_hw_task_channel_data.channel   = m_hw_task_channel;
    m_hw_task_channel_data.handler   = NULL;
    m_hw_task_channel_data.p_context = NULL;

    nrf_802154_platform_sl_lptimer_hw_task_cross_domain_connections_setup(m_hw_task_channel);
}

void nrf_802154_platform_sl_lp_timer_deinit(void)
{
    if (m_hw_task_state == HW_TASK_STATE_READY)
    {
        (void)nrf_802154_platform_sl_lptimer_hw_task_cleanup();
    }

    nrf_802154_platform_sl_lptimer_hw_task_cross_domain_connections_clear();
    nrf_802154_platform_sl_lptimer_sync_abort();

    hw_task_abort();

    (void)compare_int_lock(m_callbacks_channel);
    (void)nrfx_grtc_syscounter_cc_disable(m_callbacks_channel);
    (void)nrfx_grtc_channel_free(m_callbacks_channel);

    (void)compare_int_lock(m_sync_channel);
    (void)nrfx_grtc_syscounter_cc_disable(m_sync_channel);
    (void)nrfx_grtc_channel_free(m_sync_channel);

    (void)nrfx_grtc_channel_free(m_hw_task_channel);

    m_enabled = false;
}

uint64_t nrf_802154_platform_sl_lptimer_current_lpticks_get(void)
{
    return nrfx_grtc_syscounter_get();
}

uint64_t nrf_802154_platform_sl_lptimer_us_to_lpticks_convert(uint64_t us, bool round_up)
{
    (void)round_up;

    return us / OT_GRTC_US_PER_TICK;
}

uint64_t nrf_802154_platform_sl_lptimer_lpticks_to_us_convert(uint64_t lpticks)
{
    return lpticks * OT_GRTC_US_PER_TICK;
}

void nrf_802154_platform_sl_lptimer_schedule_at(uint64_t fire_lpticks)
{
    /* Not required to be reentrant (per API header). */
    m_enabled = true;
    compare_schedule(m_callbacks_channel, &m_callbacks_channel_data, fire_lpticks);
}

void nrf_802154_platform_sl_lptimer_disable(void)
{
    bool irq_key;

    m_enabled = false;

    irq_key = compare_int_lock(m_callbacks_channel);
    (void)nrfx_grtc_syscounter_cc_disable(m_callbacks_channel);
    compare_int_unlock(m_callbacks_channel, irq_key);
}

void nrf_802154_platform_sl_lptimer_critical_section_enter(void)
{
    nrf_802154_sl_mcu_critical_state_t state;

    nrf_802154_sl_mcu_critical_enter(state);

    m_critical_section_cnt++;

    if (m_critical_section_cnt == 1U)
    {
        m_compare_int_was_enabled      = compare_int_lock(m_callbacks_channel);
        m_sync_compare_int_was_enabled = compare_int_lock(m_sync_channel);
    }

    nrf_802154_sl_mcu_critical_exit(state);
}

void nrf_802154_platform_sl_lptimer_critical_section_exit(void)
{
    nrf_802154_sl_mcu_critical_state_t state;

    nrf_802154_sl_mcu_critical_enter(state);

    assert(m_critical_section_cnt > 0U);

    if (m_critical_section_cnt == 1U)
    {
        compare_int_unlock(m_callbacks_channel, m_compare_int_was_enabled);
        compare_int_unlock(m_sync_channel, m_sync_compare_int_was_enabled);
        m_compare_int_was_enabled      = false;
        m_sync_compare_int_was_enabled = false;
    }

    m_critical_section_cnt--;

    nrf_802154_sl_mcu_critical_exit(state);
}

uint32_t nrf_802154_platform_sl_lptimer_granularity_get(void)
{
    return (uint32_t)OT_GRTC_US_PER_TICK;
}

void nrf_802154_platform_sl_lptimer_sync_schedule_now(void)
{
    uint64_t now = nrfx_grtc_syscounter_get();

    /*
     * Despite this function's name, synchronization is not expected at the
     * current tick — add a safe margin (NCS lptimer_zephyr.c).
     */
    sync_timer_start_at(now + SYNC_SCHEDULE_NOW_MARGIN_LPTICKS);
}

void nrf_802154_platform_sl_lptimer_sync_schedule_at(uint64_t fire_lpticks)
{
    sync_timer_start_at(fire_lpticks);
}

void nrf_802154_platform_sl_lptimer_sync_abort(void)
{
    bool irq_key;

    irq_key = compare_int_lock(m_sync_channel);
    (void)nrfx_grtc_syscounter_cc_disable(m_sync_channel);
    compare_int_unlock(m_sync_channel, irq_key);
}

uint32_t nrf_802154_platform_sl_lptimer_sync_event_get(void)
{
    return nrfx_grtc_event_compare_address_get(m_sync_channel);
}

uint64_t nrf_802154_platform_sl_lptimer_sync_lpticks_get(void)
{
    return m_sync_target_lpticks;
}

/* -------------------------------------------------------------------------- */
/* Phase 3 — hw_task API                                                        */
/* -------------------------------------------------------------------------- */

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
