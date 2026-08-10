#
# Bare-metal link of closed-source Service Layer (libnrf-802154-sl.a).
# Overlay for sdk-nrfxlib nrf_802154 — upstream sl/sl/CMakeLists.txt requires Zephyr.
#

if(NOT NRF54_SL_BINARY_PATH)
    set(NRF54_SL_BINARY_PATH
        "${NRF54_802154_ROOT}/sl/sl/lib/nrf54l15_cpuapp/hard-float/libnrf-802154-sl.a"
        CACHE FILEPATH "Path to libnrf-802154-sl.a (nrf54l15_cpuapp hard-float)")
endif()

if(NOT EXISTS "${NRF54_SL_BINARY_PATH}")
    message(FATAL_ERROR "SL binary not found: ${NRF54_SL_BINARY_PATH}")
endif()

add_library(nrf-802154-sl INTERFACE)

target_link_libraries(nrf-802154-sl
    INTERFACE
        ${NRF54_SL_BINARY_PATH}
)

target_include_directories(nrf-802154-driver-interface
    INTERFACE
        ${NRF54_802154_ROOT}/sl/sl/include
        ${NRF54_802154_ROOT}/sl/sl_opensource/include
)

message(STATUS "nRF54 SL: linking binary ${NRF54_SL_BINARY_PATH}")
