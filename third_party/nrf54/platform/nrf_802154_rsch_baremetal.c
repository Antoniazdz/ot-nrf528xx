/*
 * Copyright (c) 2026, The OpenThread Authors.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Bare-metal Radio Scheduler for nRF54L RCP (replaces sl_opensource nrf_802154_sl_rsch.c stub).
 *
 * No MPSL/RAAL/coex — HFCLK is managed locally; delayed timeslots use sl_timer (GRTC CC2).
 * Reference: NCS mrt-802.15.4-driver/src/rsch/nrf_802154_rsch.c (RELAXED/PRECISE paths).
 */

#include "nrf_802154_sl_rsch.h"

#include "nrf_802154_assert.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <nrfx.h>

#include "nrf_802154_sl_timer.h"
#include "nrf_802154_sl_utils.h"
#include "rsch/nrf_802154_rsch.h"
#include "platform/nrf_802154_clock.h"
#include "nrf54_debug_stats.h"

/* nRF54L15 precondition ramp-up [us] — from NCS mrt rsch (RAAL=0 on bare-metal). */
#define PREC_TIMER_GRANULARITY_MARGIN  1U
#define PREC_IRQ_HANDLER_PROC_TIME       51U
#define PREC_HFXO_STARTUP_TIME_WORST     1650U
#define PREC_RAMP_UP_MARGIN              5U
#define PREC_RAMP_UP_TIME                (PREC_HFXO_STARTUP_TIME_WORST + PREC_IRQ_HANDLER_PROC_TIME + \
                                          PREC_TIMER_GRANULARITY_MARGIN + PREC_RAMP_UP_MARGIN)

#define RSCH_DLY_TS_POOL_SIZE NRF_802154_RSCH_DLY_TS_SLOTS

typedef struct
{
    bool                 in_use;
    rsch_dly_ts_param_t  param;
    nrf_802154_sl_timer_t timer;
} rsch_dly_ts_slot_t;

static rsch_prio_t        m_continuous_prio;
static rsch_prio_t        m_requested_prio;
static rsch_prio_t        m_last_notified_prio;
static bool               m_hfclk_ready;
static rsch_dly_ts_slot_t m_dly_ts[RSCH_DLY_TS_POOL_SIZE];

static rsch_dly_ts_slot_t *slot_find_by_id(rsch_dly_ts_id_t id)
{
    for (uint32_t i = 0; i < RSCH_DLY_TS_POOL_SIZE; i++)
    {
        if (m_dly_ts[i].in_use && (m_dly_ts[i].param.id == id))
        {
            return &m_dly_ts[i];
        }
    }

    return NULL;
}

static rsch_dly_ts_slot_t *slot_find_free(void)
{
    for (uint32_t i = 0; i < RSCH_DLY_TS_POOL_SIZE; i++)
    {
        if (!m_dly_ts[i].in_use)
        {
            return &m_dly_ts[i];
        }
    }

    return NULL;
}

static uint32_t slot_count_by_op(rsch_dly_ts_op_t op)
{
    uint32_t count = 0;

    for (uint32_t i = 0; i < RSCH_DLY_TS_POOL_SIZE; i++)
    {
        if (m_dly_ts[i].in_use && (m_dly_ts[i].param.op == op))
        {
            count++;
        }
    }

    return count;
}

static bool slot_op_limit_ok(rsch_dly_ts_op_t op)
{
    switch (op)
    {
    case RSCH_DLY_TS_OP_DTX:
        return slot_count_by_op(op) < NRF_802154_RSCH_DLY_TS_OP_DTX_SLOTS;

    case RSCH_DLY_TS_OP_DRX:
        return slot_count_by_op(op) < NRF_802154_RSCH_DLY_TS_OP_DRX_SLOTS;

    case RSCH_DLY_TS_OP_CSMACA:
        return slot_count_by_op(op) < NRF_802154_RSCH_DLY_TS_OP_CSMACA_SLOTS;

    default:
        return false;
    }
}

static rsch_prio_t delayed_max_prio_get(void)
{
    rsch_prio_t max_prio = RSCH_PRIO_IDLE;
    uint64_t    now      = nrf_802154_sl_timer_current_time_get();

    for (uint32_t i = 0; i < RSCH_DLY_TS_POOL_SIZE; i++)
    {
        rsch_dly_ts_slot_t *slot     = &m_dly_ts[i];
        rsch_prio_t         dly_prio = slot->param.prio;

        if (!slot->in_use)
        {
            continue;
        }

        if (slot->param.type == RSCH_DLY_TS_TYPE_PRECISE)
        {
            uint64_t prec_req_time = slot->param.trigger_time - PREC_RAMP_UP_TIME;

            if (nrf_802154_sl_time64_is_in_future(now, prec_req_time))
            {
                dly_prio = RSCH_PRIO_IDLE;
            }
        }

        if (dly_prio > max_prio)
        {
            max_prio = dly_prio;
        }
    }

    return max_prio;
}

static rsch_prio_t required_prio_lvl_get(void)
{
    rsch_prio_t result = delayed_max_prio_get();

    if (m_continuous_prio > result)
    {
        result = m_continuous_prio;
    }

    return result;
}

static rsch_prio_t approved_prio_lvl_get(void)
{
    /* HFCLK is the only RSCH precondition on bare-metal; when running it satisfies any op. */
    return m_hfclk_ready ? RSCH_PRIO_MAX : RSCH_PRIO_IDLE;
}

static void all_prec_update(void)
{
    // #region agent log
    g_nrf54_debug_stats.rsch_all_prec_update++;
    // #endregion

    rsch_prio_t new_prio = required_prio_lvl_get();

    if (m_requested_prio != new_prio)
    {
        m_requested_prio = new_prio;

        if (new_prio == RSCH_PRIO_IDLE)
        {
            nrf_802154_clock_hfclk_stop();
            m_hfclk_ready = false;
        }
        else if (!m_hfclk_ready)
        {
            nrf_802154_clock_hfclk_start();
        }
    }
    else if ((new_prio > RSCH_PRIO_IDLE) && !m_hfclk_ready)
    {
        nrf_802154_clock_hfclk_start();
    }
}

static void notify_core(void)
{
    rsch_prio_t approved_prio = approved_prio_lvl_get();

    // #region agent log
    g_nrf54_debug_stats.rsch_notify_core++;
    // #endregion

    if (m_last_notified_prio != approved_prio)
    {
        m_last_notified_prio = approved_prio;
        nrf_802154_rsch_continuous_prio_changed(approved_prio);
    }
}

static void timer_configure(rsch_dly_ts_slot_t *slot, uint64_t trigger_time)
{
    nrf_802154_sl_timer_init(&slot->timer);

    slot->timer.trigger_time        = trigger_time;
    slot->timer.user_data.p_pointer = slot;
    slot->timer.action_type         = NRF_802154_SL_TIMER_ACTION_TYPE_CALLBACK;

    if (slot->param.ppi_trigger_en)
    {
        slot->timer.action_type                    |= NRF_802154_SL_TIMER_ACTION_TYPE_HARDWARE;
        slot->timer.action.hardware.ppi_channel     = NRF_802154_SL_TIMER_INVALID_PPI_CHANNEL;
        slot->timer.trigger_time                    = trigger_time + slot->param.ppi_trigger_dly;
    }
}

static void delayed_timeslot_start(nrf_802154_sl_timer_t *p_timer);

static void delayed_timeslot_prec_request(nrf_802154_sl_timer_t *p_timer)
{
    rsch_dly_ts_slot_t *slot = p_timer->user_data.p_pointer;

    all_prec_update();

    timer_configure(slot, slot->param.trigger_time);
    slot->timer.action.callback.callback = delayed_timeslot_start;

    (void)nrf_802154_sl_timer_add(&slot->timer);
}

static void delayed_timeslot_start(nrf_802154_sl_timer_t *p_timer)
{
    rsch_dly_ts_slot_t *slot = p_timer->user_data.p_pointer;

    // #region agent log
    if ((slot->param.prio > RSCH_PRIO_IDLE) && !m_hfclk_ready)
    {
        g_nrf54_debug_stats.rsch_dly_start_no_hfclk++;

        all_prec_update();

        timer_configure(slot, nrf_802154_sl_timer_current_time_get() + PREC_TIMER_GRANULARITY_MARGIN);
        slot->timer.action.callback.callback = delayed_timeslot_start;
        (void)nrf_802154_sl_timer_add(&slot->timer);
        return;
    }

    g_nrf54_debug_stats.rsch_dly_start++;
    // #endregion

    if (slot->param.started_callback != NULL)
    {
        slot->param.started_callback(slot->param.id);
    }

    if (slot->param.type == RSCH_DLY_TS_TYPE_PRECISE)
    {
        slot->param.prio = RSCH_PRIO_IDLE;
        all_prec_update();
        notify_core();
    }
}

static bool relaxed_delayed_timeslot_request(rsch_dly_ts_slot_t      *slot,
                                             const rsch_dly_ts_param_t *p_param)
{
    slot->param  = *p_param;
    slot->in_use = true;

    timer_configure(slot, p_param->trigger_time);
    slot->timer.action.callback.callback = delayed_timeslot_start;

    all_prec_update();
    notify_core();

    return nrf_802154_sl_timer_add(&slot->timer) == NRF_802154_SL_TIMER_RET_SUCCESS;
}

static bool precise_delayed_timeslot_request(rsch_dly_ts_slot_t      *slot,
                                             const rsch_dly_ts_param_t *p_param)
{
    uint64_t now      = nrf_802154_sl_timer_current_time_get();
    uint64_t req_time = p_param->trigger_time - PREC_RAMP_UP_TIME;

    if (nrf_802154_sl_time64_is_in_future(now, req_time))
    {
        slot->param  = *p_param;
        slot->in_use = true;

        timer_configure(slot, req_time);
        slot->timer.action.callback.callback = delayed_timeslot_prec_request;

        all_prec_update();
        notify_core();

        return nrf_802154_sl_timer_add(&slot->timer) == NRF_802154_SL_TIMER_RET_SUCCESS;
    }

    if ((approved_prio_lvl_get() >= RSCH_PRIO_IDLE_LISTENING) &&
        nrf_802154_sl_time64_is_in_future(now, p_param->trigger_time))
    {
        slot->param  = *p_param;
        slot->in_use = true;

        timer_configure(slot, p_param->trigger_time);
        slot->timer.action.callback.callback = delayed_timeslot_start;

        all_prec_update();
        notify_core();

        return nrf_802154_sl_timer_add(&slot->timer) == NRF_802154_SL_TIMER_RET_SUCCESS;
    }

    return false;
}

static rsch_dly_ts_slot_t *slot_alloc(const rsch_dly_ts_param_t *p_param)
{
    rsch_dly_ts_slot_t *slot = slot_find_by_id(p_param->id);

    if (slot != NULL)
    {
        (void)nrf_802154_sl_timer_remove(&slot->timer);
        slot->in_use = false;
        return slot;
    }

    if (!slot_op_limit_ok(p_param->op))
    {
        return NULL;
    }

    return slot_find_free();
}

/***************************************************************************************************
 * Public API — delayed timeslots + HFCLK glue (from sl_opensource stub)
 ***************************************************************************************************/

void nrf_802154_rsch_init(void)
{
    m_continuous_prio    = RSCH_PRIO_IDLE;
    m_requested_prio     = RSCH_PRIO_IDLE;
    m_last_notified_prio = RSCH_PRIO_IDLE;
    m_hfclk_ready        = false;

    memset(m_dly_ts, 0, sizeof(m_dly_ts));
}

void nrf_802154_rsch_uninit(void)
{
    for (uint32_t i = 0; i < RSCH_DLY_TS_POOL_SIZE; i++)
    {
        if (m_dly_ts[i].in_use)
        {
            (void)nrf_802154_sl_timer_remove(&m_dly_ts[i].timer);
            m_dly_ts[i].in_use = false;
        }
    }
}

void nrf_802154_rsch_continuous_mode_priority_set(rsch_prio_t prio)
{
    m_continuous_prio = prio;

    all_prec_update();
    notify_core();
}

void nrf_802154_rsch_continuous_ended(void)
{
    /* No RAAL on bare-metal — intentionally empty. */
}

bool nrf_802154_rsch_timeslot_request(uint32_t length_us, rsch_timeslot_prio_t prio)
{
    (void)length_us;
    (void)prio;

    NRF_802154_ASSERT(m_hfclk_ready);

    return true;
}

bool nrf_802154_rsch_timeslot_is_requested(void)
{
    if (m_requested_prio > RSCH_PRIO_IDLE)
    {
        return true;
    }

    for (uint32_t i = 0; i < RSCH_DLY_TS_POOL_SIZE; i++)
    {
        if (m_dly_ts[i].in_use)
        {
            return true;
        }
    }

    return false;
}

bool nrf_802154_rsch_prec_is_approved(rsch_prec_t prec, rsch_prio_t prio)
{
    (void)prec;

    return approved_prio_lvl_get() >= prio;
}

uint32_t nrf_802154_rsch_timeslot_us_left_get(void)
{
    return UINT32_MAX;
}

void nrf_802154_clock_hfclk_ready(void)
{
    // #region agent log
    g_nrf54_debug_stats.hfclk_ready_calls++;
    // #endregion

    m_hfclk_ready = true;
    notify_core();
}

void nrf_802154_rsch_crit_sect_prio_request(rsch_prio_t prio)
{
    nrf_802154_rsch_continuous_mode_priority_set(prio);
}

void nrf_802154_rsch_crit_sect_init(void)
{
}

void nrf_802154_critical_section_rsch_enter(void)
{
}

void nrf_802154_critical_section_rsch_exit(void)
{
}

bool nrf_802154_critical_section_rsch_event_is_pending(void)
{
    return false;
}

void nrf_802154_critical_section_rsch_process_pending(void)
{
}

bool nrf_802154_rsch_delayed_timeslot_request(const rsch_dly_ts_param_t *p_dly_ts_param)
{
    rsch_dly_ts_slot_t *slot;
    bool                result = false;

    NRF_802154_ASSERT(p_dly_ts_param != NULL);
    NRF_802154_ASSERT(p_dly_ts_param->started_callback != NULL);
    NRF_802154_ASSERT(p_dly_ts_param->prio != RSCH_PRIO_IDLE);

    slot = slot_alloc(p_dly_ts_param);
    if (slot == NULL)
    {
        return false;
    }

    switch (p_dly_ts_param->type)
    {
    case RSCH_DLY_TS_TYPE_RELAXED:
        result = relaxed_delayed_timeslot_request(slot, p_dly_ts_param);
        break;

    case RSCH_DLY_TS_TYPE_PRECISE:
        result = precise_delayed_timeslot_request(slot, p_dly_ts_param);
        break;

    default:
        result = false;
        break;
    }

    if (!result)
    {
        slot->in_use = false;
        all_prec_update();
        notify_core();
    }

    return result;
}

bool nrf_802154_rsch_delayed_timeslot_cancel(rsch_dly_ts_id_t dly_ts_id, bool handler)
{
    rsch_dly_ts_slot_t       *slot = slot_find_by_id(dly_ts_id);
    nrf_802154_sl_timer_ret_t ret;
    bool                      was_active;

    (void)handler;

    if (slot == NULL)
    {
        return false;
    }

    ret        = nrf_802154_sl_timer_remove(&slot->timer);
    was_active = (ret == NRF_802154_SL_TIMER_RET_SUCCESS);

    slot->param.prio = RSCH_PRIO_IDLE;
    slot->in_use     = false;

    all_prec_update();
    notify_core();

    switch (slot->param.type)
    {
    case RSCH_DLY_TS_TYPE_PRECISE:
        return was_active;

    case RSCH_DLY_TS_TYPE_RELAXED:
    default:
        return true;
    }
}

bool nrf_802154_rsch_delayed_timeslot_priority_update(rsch_dly_ts_id_t dly_ts_id,
                                                      rsch_prio_t      dly_ts_prio)
{
    rsch_dly_ts_slot_t *slot = slot_find_by_id(dly_ts_id);

    if ((slot == NULL) || (slot->param.prio == RSCH_PRIO_IDLE))
    {
        return false;
    }

    slot->param.prio = dly_ts_prio;

    all_prec_update();
    notify_core();

    return true;
}

bool nrf_802154_rsch_delayed_timeslot_ppi_update(uint32_t ppi_channel)
{
    for (uint32_t i = 0; i < RSCH_DLY_TS_POOL_SIZE; i++)
    {
        rsch_dly_ts_slot_t *slot = &m_dly_ts[i];

        if (!slot->in_use || !slot->param.ppi_trigger_en)
        {
            continue;
        }

        if ((slot->timer.action_type & NRF_802154_SL_TIMER_ACTION_TYPE_HARDWARE) != 0U)
        {
            nrf_802154_sl_timer_ret_t ret;

            ret = nrf_802154_sl_timer_update_ppi(&slot->timer, ppi_channel);

            return ret == NRF_802154_SL_TIMER_RET_SUCCESS;
        }
    }

    return true;
}

bool nrf_802154_rsch_delayed_timeslot_time_to_start_get(rsch_dly_ts_id_t dly_ts_id,
                                                        uint64_t       * p_time_to_start)
{
    rsch_dly_ts_slot_t *slot = slot_find_by_id(dly_ts_id);
    uint64_t            now;

    if ((slot == NULL) || (p_time_to_start == NULL))
    {
        return false;
    }

    now               = nrf_802154_sl_timer_current_time_get();
    *p_time_to_start  = 0;

    if (nrf_802154_sl_time64_is_in_future(now, slot->param.trigger_time))
    {
        *p_time_to_start = slot->param.trigger_time - now;
    }

    return true;
}

#if defined(CONFIG_SOC_SERIES_BSIM_NRFXX)
uint32_t nrf_802154_rsch_delayed_timeslot_time_to_hw_trigger_get(void)
{
    return 0;
}
#endif

void nrf_802154_clock_hfclk_latency_set(uint32_t latency_us)
{
    (void)latency_us;
}

void nrf_802154_rsch_continuous_prio_changed(rsch_prio_t prio)
{
    /* Bare-metal: no crit-sect RSCH event queue; core handler is safe to call directly. */
    extern void nrf_802154_rsch_crit_sect_prio_changed(rsch_prio_t prio);

    nrf_802154_rsch_crit_sect_prio_changed(prio);
}
