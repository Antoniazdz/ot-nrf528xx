/*
 * Copyright (c) 2026, The OpenThread Authors.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Bare-metal nRF54L15 implementation of nrf_802154_sl_timer (802.15.4 driver
 * timer scheduler). Replaces the Zephyr stub in
 * sl/sl_opensource/src/nrf_802154_sl_timer.c when wired in CMake.
 *
 * Step 1: module init, current time, timer instance init/deinit, timer_coord stubs,
 *         and link stubs for add/remove/handler (scheduler in later steps).
 *
 * POC (NRF54_POC_MINIMAL_TIMERS=ON): platform is nrf_802154_platform_sl_lptimer_stub.c;
 * sl_timer_add/remove/handler are no-ops — enough for UART/Spinel bring-up.
 *
 * Full build (NRF54_POC_MINIMAL_TIMERS=OFF): platform is
 * nrf_802154_platform_sl_lptimer.c + nrf_802154_platform_sl_lptimer_grtc_hw_task.c.
 *
 * Reference: third_party/NordicSemiconductor/drivers/radio/timer_scheduler/
 *            nrf_802154_timer_sched.c (nRF52 timer_sched — same role, later steps)
 */

 #include "nrf_802154_sl_timer.h"

 #include <assert.h>
 #include <stdbool.h>
 #include <stdint.h>
 #include <string.h>
 
 #include <nrf.h>
 
 #include "platform/nrf_802154_platform_sl_lptimer.h"
 #include "timer/nrf_802154_timer_coord.h"

 #include "nrf54_debug_stats.h"

 
 #define MIN_LPTICK_COMPARE_EVENT_TICKS 2ULL
 
 typedef struct
 {
     nrf_802154_sl_timer_t *p_next;
     bool                     in_queue;
     bool                     hw_fired;
     uint64_t                 fire_lpticks;
 } sl_timer_priv_t;
 
 static inline sl_timer_priv_t *sl_timer_priv(nrf_802154_sl_timer_t *p_timer)
 {
     return (sl_timer_priv_t *)&p_timer->priv;
 }
 
 static inline nrf_802154_sl_timer_t **sl_timer_next_link(nrf_802154_sl_timer_t *p_timer)
 {
     return &sl_timer_priv(p_timer)->p_next;
 }
 
 static volatile uint8_t               m_timer_mutex;
 static volatile uint8_t               m_fired_mutex;
 static volatile uint8_t               m_queue_changed_cntr;
 static volatile nrf_802154_sl_timer_t *mp_head;
 
 static inline bool mutex_trylock(volatile uint8_t *p_mutex)
 {
     do
     {
         volatile uint8_t mutex_value = __LDREXB(p_mutex);
 
         if (mutex_value)
         {
             __CLREX();
             return false;
         }
     } while (__STREXB(1, p_mutex));
 
     __DMB();
 
     return true;
 }
 
 static inline void mutex_unlock(volatile uint8_t *p_mutex)
 {
     __DMB();
     *p_mutex = 0;
 }
 
 static inline void queue_cntr_bump(void)
 {
     volatile uint8_t cntr;
 
     do
     {
         cntr = __LDREXB(&m_queue_changed_cntr);
     } while (__STREXB(cntr + 1, &m_queue_changed_cntr));
 
     __DMB();
 }
 
 static inline bool is_timer_prior(const nrf_802154_sl_timer_t *p_timer_1,
                                   const nrf_802154_sl_timer_t *p_timer_2)
 {
     return p_timer_1->trigger_time < p_timer_2->trigger_time;
 }
 
 static inline bool timer_has_callback(const nrf_802154_sl_timer_t *p_timer)
 {
     return (p_timer->action_type & NRF_802154_SL_TIMER_ACTION_TYPE_CALLBACK) != 0U;
 }
 
 static inline bool timer_has_hardware(const nrf_802154_sl_timer_t *p_timer)
 {
     return (p_timer->action_type & NRF_802154_SL_TIMER_ACTION_TYPE_HARDWARE) != 0U;
 }
 
 static nrf_802154_sl_timer_ret_t platform_result_to_timer_ret(nrf_802154_sl_lptimer_platform_result_t result)
 {
     switch (result)
     {
     case NRF_802154_SL_LPTIMER_PLATFORM_SUCCESS:
         return NRF_802154_SL_TIMER_RET_SUCCESS;
 
     case NRF_802154_SL_LPTIMER_PLATFORM_TOO_LATE:
         return NRF_802154_SL_TIMER_RET_TOO_LATE;
 
     case NRF_802154_SL_LPTIMER_PLATFORM_TOO_DISTANT:
         return NRF_802154_SL_TIMER_RET_TOO_DISTANT;
 
     case NRF_802154_SL_LPTIMER_PLATFORM_NO_RESOURCES:
         return NRF_802154_SL_TIMER_RET_NO_RESOURCES;
 
     default:
         return NRF_802154_SL_TIMER_RET_BAD_REQUEST;
     }
 }
 
 static void handle_timer(void)
 {
     volatile nrf_802154_sl_timer_t *p_head;
     uint8_t                           queue_cntr;
 
     do
     {
         queue_cntr = m_queue_changed_cntr;
         p_head     = mp_head;
 
         if (mutex_trylock(&m_timer_mutex))
         {
            if ((p_head == NULL) || !timer_has_callback((const nrf_802154_sl_timer_t *)p_head))
            {
                nrf_802154_platform_sl_lptimer_disable();
                mutex_unlock(&m_timer_mutex);
            }
            else if (p_head == mp_head)
            {
                nrf_802154_sl_timer_t *p_head_timer = (nrf_802154_sl_timer_t *)p_head;
                uint64_t               fire_lpticks;
                uint64_t               now_lpticks;
                bool                   fire_immediately;

                fire_lpticks = nrf_802154_platform_sl_lptimer_us_to_lpticks_convert(p_head_timer->trigger_time,
                                                                                    false);
                sl_timer_priv(p_head_timer)->fire_lpticks = fire_lpticks;

                now_lpticks      = nrf_802154_platform_sl_lptimer_current_lpticks_get();
                fire_immediately = fire_lpticks <= now_lpticks;

                mutex_unlock(&m_timer_mutex);

                if (fire_immediately)
                {
                    g_nrf54_debug_stats.sl_timer_fire_immediate++;
                    nrf_802154_sl_timer_handler(now_lpticks);
                    continue;
                }

                nrf_802154_platform_sl_lptimer_schedule_at(fire_lpticks);
            }
            else
            {
                mutex_unlock(&m_timer_mutex);
            }
         }
     } while (queue_cntr != m_queue_changed_cntr);
 }
 
 static bool timer_remove(nrf_802154_sl_timer_t *p_timer, bool *p_was_in_queue)
 {
     assert(p_timer != NULL);
 
     nrf_802154_sl_timer_t         **pp_item;
     nrf_802154_sl_timer_t * volatile p_next;
     nrf_802154_sl_timer_t          *p_cur;
     uint8_t                           queue_cntr;
     bool                              timer_start;
     bool                              timer_stop;
 
     while (true)
     {
         queue_cntr  = m_queue_changed_cntr;
         pp_item     = (nrf_802154_sl_timer_t **)&mp_head;
         p_next      = NULL;
         p_cur       = NULL;
         timer_start = false;
         timer_stop  = false;
 
         while (true)
         {
             p_cur = (nrf_802154_sl_timer_t *)__LDREXW((uint32_t *)pp_item);
 
             if ((p_cur == NULL) || (p_cur == p_timer))
             {
                 break;
             }
 
             pp_item = sl_timer_next_link(p_cur);
         }
 
         if (queue_cntr != m_queue_changed_cntr)
         {
             continue;
         }
 
         if (p_cur == p_timer)
         {
             p_next = sl_timer_priv(p_cur)->p_next;
 
             if (p_cur == mp_head)
             {
                 if (p_next != NULL)
                 {
                     timer_start = true;
                 }
                 else
                 {
                     timer_stop = true;
                 }
             }
         }
         else
         {
             __CLREX();
             break;
         }
 
         if (!__STREXW((uint32_t)p_next, (uint32_t *)pp_item))
         {
             queue_cntr_bump();
             break;
         }
     }
 
     bool was_in_queue = false;
 
     if (p_cur != NULL)
     {
         was_in_queue = true;
 
         uint32_t temp;
 
         do
         {
             temp = __LDREXW((uint32_t *)sl_timer_next_link(p_cur));
             assert((void *)temp != p_cur);
         } while (__STREXW(temp, (uint32_t *)sl_timer_next_link(p_cur)));
 
         sl_timer_priv(p_cur)->in_queue = false;
     }
 
     if (p_was_in_queue != NULL)
     {
         *p_was_in_queue = was_in_queue;
     }
 
     return (timer_start || timer_stop);
 }
 
 static void scheduler_reset(void)
 {
     mp_head              = NULL;
     m_timer_mutex        = 0;
     m_fired_mutex        = 0;
     m_queue_changed_cntr = 0;
 }
 
 /* -------------------------------------------------------------------------- */
 /* Timer coordinator (stubs while NRF_802154_FRAME_TIMESTAMP_ENABLED=0)       */
 /* Ref: sl/sl_opensource/src/nrf_802154_sl_timer.c                            */
 /* -------------------------------------------------------------------------- */
 
 void nrf_802154_timer_coord_init(void)
 {
 }
 
 void nrf_802154_timer_coord_uninit(void)
 {
 }
 
 void nrf_802154_timer_coord_start(void)
 {
 }
 
 void nrf_802154_timer_coord_stop(void)
 {
 }
 
 void nrf_802154_timer_coord_timestamp_prepare(const nrf_802154_sl_event_handle_t *p_event)
 {
     (void)p_event;
 }
 
 bool nrf_802154_timer_coord_timestamp_get(uint64_t *p_timestamp)
 {
     (void)p_timestamp;
 
     return false;
 }
 
 /* -------------------------------------------------------------------------- */
 /* Module init — platform GRTC CC2/CC3/CC8 (requires nrfx_grtc_init first)    */
 /* -------------------------------------------------------------------------- */
 
 void nrf_802154_sl_timer_module_init(void)
 {
     scheduler_reset();
     nrf_802154_platform_sl_lp_timer_init();
 }
 
 void nrf_802154_sl_timer_module_uninit(void)
 {
     nrf_802154_platform_sl_lp_timer_deinit();
 }
 
 uint64_t nrf_802154_sl_timer_current_time_get(void)
 {
     return nrf_802154_platform_sl_lptimer_lpticks_to_us_convert(
         nrf_802154_platform_sl_lptimer_current_lpticks_get());
 }
 
 void nrf_802154_sl_timer_init(nrf_802154_sl_timer_t *p_timer)
 {
     memset(&p_timer->priv, 0, sizeof(p_timer->priv));
 }
 
 void nrf_802154_sl_timer_deinit(nrf_802154_sl_timer_t *p_timer)
 {
     memset(&p_timer->priv, 0, sizeof(p_timer->priv));
 }
 
 /* -------------------------------------------------------------------------- */
 /* Scheduler — add/remove/handler                                             */
 /* -------------------------------------------------------------------------- */
 
 void nrf_802154_sl_timer_handler(uint64_t now_lpticks)
 {
    g_nrf54_debug_stats.sl_timer_handler_enter++;
     uint64_t now_us = nrf_802154_platform_sl_lptimer_lpticks_to_us_convert(now_lpticks);
 
     if (mutex_trylock(&m_fired_mutex))
     {
         nrf_802154_sl_timer_t *p_timer = (nrf_802154_sl_timer_t *)mp_head;
         nrf_802154_sl_timer_callback_t callback = NULL;
         bool                              was_in_queue = false;

         if ((p_timer != NULL) && (p_timer->trigger_time <= now_us))
         {
             if (timer_has_callback(p_timer))
             {
                 callback = p_timer->action.callback.callback;
             }

             if (timer_has_hardware(p_timer))
             {
                 sl_timer_priv(p_timer)->hw_fired = true;
                 (void)nrf_802154_platform_sl_lptimer_hw_task_cleanup();
             }

             (void)timer_remove(p_timer, &was_in_queue);
         }

         mutex_unlock(&m_fired_mutex);

         if (was_in_queue && (callback != NULL))
         {
             callback(p_timer);
         }
     }
 
     handle_timer();
 }
 
 void nrf_802154_sl_timestamper_synchronized(void)
 {
 }
 
 nrf_802154_sl_timer_ret_t nrf_802154_sl_timer_add(nrf_802154_sl_timer_t *p_timer)
 {
     nrf_802154_sl_lptimer_platform_result_t hw_result = NRF_802154_SL_LPTIMER_PLATFORM_SUCCESS;
 
     assert(p_timer != NULL);
     assert(p_timer->action_type != 0U);
 
     if (timer_has_callback(p_timer))
     {
         assert(p_timer->action.callback.callback != NULL);
     }
 
     sl_timer_priv(p_timer)->hw_fired = false;
     sl_timer_priv(p_timer)->fire_lpticks =
         nrf_802154_platform_sl_lptimer_us_to_lpticks_convert(p_timer->trigger_time, false);
 
     if (timer_has_hardware(p_timer))
     {
         hw_result = nrf_802154_platform_sl_lptimer_hw_task_prepare(
             sl_timer_priv(p_timer)->fire_lpticks,
             p_timer->action.hardware.ppi_channel);
 
         if (hw_result != NRF_802154_SL_LPTIMER_PLATFORM_SUCCESS)
         {
             return platform_result_to_timer_ret(hw_result);
         }
     }
 
     if (timer_remove(p_timer, NULL))
     {
         handle_timer();
     }
 
     nrf_802154_sl_timer_t ** pp_item;
     nrf_802154_sl_timer_t  * p_next;
     uint8_t                   queue_cntr;
 
     while (true)
     {
         queue_cntr = m_queue_changed_cntr;
         pp_item    = (nrf_802154_sl_timer_t **)&mp_head;
         p_next     = NULL;
 
         // Search the current queue to find appropriate position to insert timer.
         while (true)
         {
             nrf_802154_sl_timer_t * p_cur = (nrf_802154_sl_timer_t *)__LDREXW((uint32_t *)pp_item);
 
             assert(p_cur != p_timer);
 
             if (p_cur == NULL)
             {
                 // No HEAD or insert at the end.
                 p_next = NULL;
                 break;
             }
 
             if (is_timer_prior(p_timer, p_cur))
             {
                 // Insert at the beginning with existing HEAD or somewhere in the middle.
                 p_next = p_cur;
                 break;
             }
 
             pp_item = sl_timer_next_link(p_cur);
         }
 
         if (queue_cntr != m_queue_changed_cntr)
         {
             // Higher priority modified the queue while iterating, try again.
             continue;
         }
 
         assert(p_next != p_timer);
         sl_timer_priv(p_timer)->p_next = p_next;
 
         if (!__STREXW((uint32_t)p_timer, (uint32_t *)pp_item))
         {
             // Exit, if exclusive access succeeds.
             queue_cntr_bump();
             break;
         }
     }
 
     sl_timer_priv(p_timer)->in_queue = true;
 
     if (mp_head == p_timer)
     {
         handle_timer();
     }
 
     return NRF_802154_SL_TIMER_RET_SUCCESS;
 }
 
 nrf_802154_sl_timer_ret_t nrf_802154_sl_timer_remove(nrf_802154_sl_timer_t *p_timer)
 {
     bool was_in_queue;
 
     if (timer_remove(p_timer, &was_in_queue))
     {
         handle_timer();
     }
 
     if (was_in_queue && timer_has_hardware(p_timer) && !sl_timer_priv(p_timer)->hw_fired)
     {
         (void)nrf_802154_platform_sl_lptimer_hw_task_cleanup();
     }
 
     return was_in_queue ? NRF_802154_SL_TIMER_RET_SUCCESS : NRF_802154_SL_TIMER_RET_INACTIVE;
 }
 
 nrf_802154_sl_timer_ret_t nrf_802154_sl_timer_update_ppi(nrf_802154_sl_timer_t *p_timer, uint32_t ppi_chn)
 {
     nrf_802154_sl_lptimer_platform_result_t result;
 
     if ((p_timer == NULL) || !timer_has_hardware(p_timer))
     {
         return NRF_802154_SL_TIMER_RET_BAD_REQUEST;
     }
 
     if (sl_timer_priv(p_timer)->hw_fired)
     {
         return NRF_802154_SL_TIMER_RET_BAD_REQUEST;
     }
 
     result = nrf_802154_platform_sl_lptimer_hw_task_update_ppi(ppi_chn);
 
     return (result == NRF_802154_SL_LPTIMER_PLATFORM_SUCCESS) ? NRF_802154_SL_TIMER_RET_SUCCESS :
                                                                 NRF_802154_SL_TIMER_RET_TOO_LATE;
 }