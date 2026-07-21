#
# Platform layer required by Nordic nrf_802154 CMake.
#

set(NRF54_802154_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../nordic/drivers/nrf_802154)

# GRTC lptimer platform (CC2/CC3/CC8 + DPPI). Set ON for link-only POC without radio timing.
#   OFF — nrf_802154_platform_sl_lptimer.c + nrf_802154_platform_sl_lptimer_grtc_hw_task.c (default)
#   ON  — nrf_802154_platform_sl_lptimer_stub.c
option(NRF54_POC_MINIMAL_TIMERS "Use minimal POC stub for 802.15.4 SL lptimer platform" OFF)

# CC2 callback timer only (no CC8/DPPI). CC8 cross-domain wiring breaks CCA on bare-metal nRF54L15.
option(NRF54_LPTIMER_CC2_ONLY "GRTC lptimer CC2 callbacks only; disable CC8 hw_task+DPPI" ON)

# Bisect test G: MINIMAL=OFF but link lptimer_stub.c (not lptimer.c); grtc_hw_task.c still built.
option(NRF54_LPTIMER_BISECT_STUB "Bisect G: link lptimer_stub.c with MINIMAL_TIMERS=OFF" OFF)

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
    nrf54_debug_stats.c
    nrf_802154_clock_platform.c
    nrf_802154_clock_callbacks.c
    nrf_802154_irq_platform.c
    nrf_802154_temperature_platform.c
    nrf_802154_delayed_trx_stub.c
    nrf_802154_sl_timer_baremetal.c
    nrf_802154_rsch_baremetal.c
)

if(NRF54_POC_MINIMAL_TIMERS)
    list(APPEND NRF54_PLATFORM_SOURCES nrf_802154_platform_sl_lptimer_stub.c)
elseif(NRF54_LPTIMER_BISECT_STUB)
    list(APPEND NRF54_PLATFORM_SOURCES
        nrf_802154_platform_sl_lptimer_stub.c
        nrf_802154_platform_sl_lptimer_grtc_hw_task.c
    )
else()
    list(APPEND NRF54_PLATFORM_SOURCES nrf_802154_platform_sl_lptimer.c)
    if(NOT NRF54_LPTIMER_CC2_ONLY)
        list(APPEND NRF54_PLATFORM_SOURCES nrf_802154_platform_sl_lptimer_grtc_hw_task.c)
    endif()
endif()

target_sources(nrf-802154-platform PRIVATE ${NRF54_PLATFORM_SOURCES})

if(NRF54_POC_MINIMAL_TIMERS)
    target_compile_definitions(nrf-802154-platform PRIVATE NRF54_LPTIMER_CC2_STUB_BISECT=1)
    message(STATUS "nRF54 platform: POC minimal lptimer stub")
elseif(NRF54_LPTIMER_BISECT_STUB)
    target_compile_definitions(nrf-802154-platform PRIVATE NRF54_LPTIMER_CC2_STUB_BISECT=1)
    message(STATUS "nRF54 platform: bisect G — lptimer_stub.c (+ optional grtc_hw_task.c)")
else()
    if(NRF54_LPTIMER_CC2_ONLY)
        target_compile_definitions(nrf-802154-platform PRIVATE NRF54_LPTIMER_CC2_ONLY_BISECT=1)
        message(STATUS "nRF54 platform: GRTC lptimer CC2 only (CSMA timers; CC8/DPPI disabled)")
    else()
        target_compile_definitions(nrf-802154-platform PRIVATE NRF54_LPTIMER_CC2_ONLY_BISECT=0)
        message(STATUS "nRF54 platform: GRTC lptimer CC2+CC8+DPPI (experimental)")
    endif()
    target_compile_definitions(nrf-802154-platform PRIVATE NRF54_LPTIMER_CC2_STUB_BISECT=0)
endif()
