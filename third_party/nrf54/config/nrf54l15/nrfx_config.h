/*
 * nrfx configuration for nRF54L15 application core.
 */

#ifndef NRFX_CONFIG_H__
#define NRFX_CONFIG_H__

/* OT alarm (alarm_nrf54.c) and future SL lptimer platform code. */
#define NRFX_GRTC_ENABLED  1
#define NRFX_CLOCK_ENABLED 1
#define NRFX_RRAMC_ENABLED 1

#include <nrfx_config_nrf54l15_application.h>

#endif // NRFX_CONFIG_H__
