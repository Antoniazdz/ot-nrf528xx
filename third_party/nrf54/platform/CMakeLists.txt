#
# Platform layer required by Nordic nrf_802154 CMake.
#

set(NRF54_802154_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../nrfxlib/nrf_802154)

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
    platform_gppi_nrf54l.c
    nrf_802154_clock_platform.c
    nrf_802154_clock_callbacks.c
    nrf_802154_irq_platform.c
    nrf_802154_temperature_platform.c
    nrf_802154_platform_sl_lptimer.c
    nrf_802154_platform_sl_lptimer_grtc_hw_task.c
)

if(SL_OPENSOURCE)
    list(APPEND NRF54_PLATFORM_SOURCES nrf_raal_single_phy.c)
endif()
if(NOT SL_OPENSOURCE)
    list(APPEND NRF54_PLATFORM_SOURCES
        ${NRF54_802154_ROOT}/sl/sl_opensource/src/nrf_802154_sl_fem.c
    )
endif()

target_sources(nrf-802154-platform PRIVATE ${NRF54_PLATFORM_SOURCES})

if(SL_OPENSOURCE)
    message(STATUS "nRF54 platform: GRTC lptimer CC2+CC8+DPPI (open-source SL)")
else()
    message(STATUS "nRF54 platform: GRTC lptimer CC2+CC8+DPPI + SL-binary FEM glue")
endif()
