/**
 * Minimal sdk_config.h stub for nRF54L15 bring-up.
 *
 * Add #defines here when the build reports missing NRFX_* or NRF_* options.
 * Do not copy the full nRF52 sdk_config.h — grow this file incrementally.
 */

#ifndef SDK_CONFIG_H
#define SDK_CONFIG_H

#ifdef USE_APP_CONFIG
#include "app_config.h"
#endif

/* --- enable below as vendor SDK files are added --- */

/* #define NRFX_CLOCK_ENABLED 0 */
/* #define NRFX_TIMER_ENABLED 0 */
/* #define NRFX_GPIOTE_ENABLED 0 */

#endif // SDK_CONFIG_H
