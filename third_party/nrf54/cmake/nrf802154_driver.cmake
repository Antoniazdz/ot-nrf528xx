#
# Bare-metal integration of sdk-nrfxlib nrf_802154 (common + driver + SL).
# Skips Zephyr-only sl/sl/CMakeLists.txt and serialization (not used on RCP).
#

add_library(nrf-802154-driver-interface INTERFACE)
add_library(nrf-802154-serialization-interface INTERFACE)

add_subdirectory(
    ${NRF54_802154_ROOT}/common
    ${CMAKE_BINARY_DIR}/nrf54_nrf_802154/common
)

add_subdirectory(
    ${NRF54_802154_ROOT}/driver
    ${CMAKE_BINARY_DIR}/nrf54_nrf_802154/driver
)

target_include_directories(nrf-802154-driver-interface
    INTERFACE
        ${NRF54_802154_ROOT}/sl/include
)

if(SL_OPENSOURCE)
    add_subdirectory(
        ${NRF54_802154_ROOT}/sl/sl_opensource
        ${CMAKE_BINARY_DIR}/nrf54_nrf_802154/sl_opensource
    )
else()
    include(${NRF54_ROOT}/cmake/nrf802154_sl_binary.cmake)
endif()
