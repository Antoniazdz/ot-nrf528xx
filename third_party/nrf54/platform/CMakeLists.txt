#
# Platform layer required by Nordic nrf_802154 CMake.
#

set(NRF54_802154_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../nordic/drivers/nrf_802154)

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

target_sources(nrf-802154-platform
    PRIVATE
        nrf54_debug_stats.c
        nrf_802154_clock_platform.c
        nrf_802154_clock_callbacks.c
        nrf_802154_irq_platform.c
        nrf_802154_temperature_platform.c
        nrf_802154_delayed_trx_stub.c
        nrf_802154_platform_sl_lptimer.c
        nrf_802154_platform_sl_lptimer_grtc_hw_task.c
)

target_compile_definitions(nrf-802154-platform
    PRIVATE
        NRF54_LPTIMER_CC2_ONLY_BISECT=0
        NRF54_LPTIMER_CC2_STUB_BISECT=0
)

message(STATUS "nRF54 platform: GRTC lptimer CC2+CC8+DPPI")
