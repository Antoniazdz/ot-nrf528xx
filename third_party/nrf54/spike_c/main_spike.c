/*
 * Variant C spike — gate G2: nrf_802154_rsch_init() without libmpsl.a
 *
 * PASS: RTT prints "rsch_init OK" → sym_* stub works, link feasible.
 * FAIL: hardfault before message → pivot to variant A (MPSL + SL-binary).
 */

#include <stdint.h>

#include "rsch/nrf_802154_rsch.h"
#include "platform/nrf_802154_platform_sl_lptimer.h"
#include "platform/nrf_802154_platform_timestamper.h"

#include <nrfx_clock_lfclk.h>
#include <nrfx_clock_xo.h>
#include <nrfx_grtc.h>

#include "SEGGER_RTT.h"

extern void nrf_raal_init(void);

static void lfclk_handler(nrfx_clock_lfclk_evt_type_t event)
{
    (void)event;
}

#if NRF_CLOCK_HAS_XO
static void xo_handler(nrfx_clock_xo_event_type_t event)
{
    (void)event;
}
#endif

static void spike_clocks_init(void)
{
    if (!nrfx_grtc_init_check())
    {
        (void)nrfx_grtc_init(0);
    }

    if (!nrfx_clock_lfclk_init_check())
    {
        (void)nrfx_clock_lfclk_init(lfclk_handler);
        (void)nrfx_clock_lfclk_start();
    }

#if NRF_CLOCK_HAS_XO
    if (!nrfx_clock_xo_init_check())
    {
        (void)nrfx_clock_xo_init(xo_handler);
    }
#endif
}

int main(void)
{
    spike_clocks_init();

    SEGGER_RTT_WriteString(0, "spike-c: init lptimer/timestamper/raal\r\n");

    nrf_802154_platform_sl_lp_timer_init();
    nrf_802154_platform_timestamper_init();
    nrf_raal_init();

    SEGGER_RTT_WriteString(0, "spike-c: calling nrf_802154_rsch_init()...\r\n");

    nrf_802154_rsch_init();

    SEGGER_RTT_WriteString(0, "spike-c: PASS — rsch_init OK (sym_* gate)\r\n");

    while (1)
    {
        __asm volatile("wfi");
    }
}
