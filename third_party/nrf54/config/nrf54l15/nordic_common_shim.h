/*
 * Bare-metal shim: Nordic 802.15.4 driver (hal_nordic) uses MAX()/MIN().
 * On NCS/Zephyr these come from nordic_common.h; force-include this for nrf-802154-driver.
 */

#ifndef NRF54_NORDIC_COMMON_SHIM_H_
#define NRF54_NORDIC_COMMON_SHIM_H_

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#endif

#endif // NRF54_NORDIC_COMMON_SHIM_H_
