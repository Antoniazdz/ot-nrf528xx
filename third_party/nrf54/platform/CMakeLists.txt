#
# Platform layer required by Nordic nrf_802154 CMake.
#

set(NRF54_802154_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../nordic/drivers/nrf_802154)

# POC: stub radio lptimer (no CC2/CC3/CC8, no DPPI). Full sources stay in-tree.
#   ON  — nrf_802154_platform_sl_lptimer_stub.c (default)
#   OFF — nrf_802154_platform_sl_lptimer.c + nrf_802154_platform_sl_lptimer_grtc_hw_task.c
option(NRF54_POC_MINIMAL_TIMERS "Use minimal POC stub for 802.15.4 SL lptimer platform" ON)

add_library(nrf-802154-platform STATIC)

target_include_directories(nrf-802154-platform
    PUBLIC
        ${NRF54_802154_ROOT}/driver/include/platform
        ${NRF54_802154_ROOT}/sl/include
        ${NRF54_802154_ROOT}/sl/include/platform
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/nrf54l15
        ${CMAKE_CURRENT_SOURCE_DIR}/../nordic/nrfx/bsp/stable/soc
        ${CMAKE_CURRENT_SOURCE_DIR}/../nordic/nrfx/bsp/stable/soc/interconnect
        ${CMAKE_CURRENT_SOURCE_DIR}/../nordic/nrfx
        ${CMAKE_CURRENT_SOURCE_DIR}/../nordic/nrfx/bsp/stable/templates
        ${CMAKE_CURRENT_SOURCE_DIR}/../nordic/nrfx/templates
)

set(NRF54_PLATFORM_SOURCES
    nrf_802154_clock_platform.c
    nrf_802154_sl_timer_baremetal.c
)

if(NRF54_POC_MINIMAL_TIMERS)
    list(APPEND NRF54_PLATFORM_SOURCES nrf_802154_platform_sl_lptimer_stub.c)
else()
    list(APPEND NRF54_PLATFORM_SOURCES
        nrf_802154_platform_sl_lptimer.c
        nrf_802154_platform_sl_lptimer_grtc_hw_task.c
    )
endif()

target_sources(nrf-802154-platform PRIVATE ${NRF54_PLATFORM_SOURCES})

if(NRF54_POC_MINIMAL_TIMERS)
    message(STATUS "nRF54 platform: POC minimal lptimer stub (set NRF54_POC_MINIMAL_TIMERS=OFF for full GRTC lptimer)")
endif()
