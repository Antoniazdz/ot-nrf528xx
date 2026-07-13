#
#  Copyright (c) 2020, The OpenThread Authors.
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#  3. Neither the name of the copyright holder nor the
#     names of its contributors may be used to endorse or promote products
#     derived from this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
#  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
#  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGE.
#

# nRF54L — Nordic hal_nordic tree under nordic/, wired via Nordic nrf_802154 CMake.

cmake_policy(SET CMP0079 NEW)

set(NRF54_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(NORDIC_ROOT ${NRF54_ROOT}/nordic)

set(NRF54_NRFX_ROOT ${NORDIC_ROOT}/nrfx)
set(NRF54_MDK_ROOT ${NRF54_NRFX_ROOT}/bsp/stable/mdk)
set(NRF54_SOC_ROOT ${NRF54_NRFX_ROOT}/bsp/stable/soc)
set(NRF54_CONFIG_ROOT ${NRF54_ROOT}/config/nrf54l15)
set(NRF54_CMSIS_ROOT ${NRF54_ROOT}/cmsis)
set(NRF54_802154_ROOT ${NORDIC_ROOT}/drivers/nrf_802154)

# nrfx 3.x layout (hal_nordic) — different paths than nRF52 sdk/nrfx/
set(NRF54_SDK_INCLUDES
    ${NRF54_CONFIG_ROOT}
    ${NRF54_CMSIS_ROOT}
    ${NRF54_NRFX_ROOT}/bsp/stable
    ${NRF54_NRFX_ROOT}/bsp/stable/templates
    ${NRF54_NRFX_ROOT}/templates
    ${NRF54_NRFX_ROOT}
    ${NRF54_NRFX_ROOT}/hal
    ${NRF54_NRFX_ROOT}/haly
    ${NRF54_NRFX_ROOT}/helpers
    ${NRF54_NRFX_ROOT}/drivers
    ${NRF54_NRFX_ROOT}/drivers/include
    ${NRF54_MDK_ROOT}
    ${NRF54_SOC_ROOT}
    ${NRF54_SOC_ROOT}/irqs
    ${NRF54_SOC_ROOT}/interconnect
)

set(NRF54_RADIO_INCLUDES
    ${NRF54_802154_ROOT}/driver/include
    ${NRF54_802154_ROOT}/sl/include
    ${NRF54_802154_ROOT}/common/include
    ${PROJECT_SOURCE_DIR}/openthread/include
    ${PROJECT_SOURCE_DIR}/openthread/src
    ${PROJECT_SOURCE_DIR}/openthread/src/core
)

set(NRF54_FLAG
    -DCONFIG_GPIO_AS_PINRESET
    -DENABLE_FEM=1
    -DUSE_APP_CONFIG=1
)

set(NRF54_CHIP_DEFINES
    -DNRF54L15_XXAA
    -DNRF_APPLICATION
)

set(NRF54_COMMON_DEFINES
    ${NRF54_FLAG}
    ${NRF54_CHIP_DEFINES}
    -DNRF_802154_PROJECT_CONFIG=\"platform-config.h\"
    -D__HEAP_SIZE=0
    -D__STACK_SIZE=2048
)

# Step 1: platform lib (stub) — required before Nordic driver subdirectory.
add_subdirectory(platform)

# Step 2: Nordic 802.15.4 driver via its own CMake.
set(SL_OPENSOURCE ON CACHE BOOL "Use open-source 802.15.4 Service Layer" FORCE)

if(NOT NRF_802154_PROJECT_CONFIG)
    set(NRF_802154_PROJECT_CONFIG platform-config.h)
endif()

add_subdirectory(${NRF54_802154_ROOT} nrf54_nrf_802154_build)

# Bare-metal sl_timer is built in nrf-802154-platform; exclude Zephyr stub from SL.
if(TARGET nrf-802154-sl)
    get_target_property(_nrf802154_sl_sources nrf-802154-sl SOURCES)
    if(_nrf802154_sl_sources)
        list(FILTER _nrf802154_sl_sources EXCLUDE REGEX "nrf_802154_sl_timer\\.c$")
        set_target_properties(nrf-802154-sl PROPERTIES SOURCES "${_nrf802154_sl_sources}")
    endif()
endif()

# Circular dependency between platform and sl (per Nordic docs).
target_link_libraries(nrf-802154-platform PRIVATE nrf-802154-sl)

# Propagate chip defines and include paths to Nordic targets.
foreach(_nrf802154_lib
    nrf-802154-driver
    nrf-802154-sl
    nrf-802154-common
    nrf-802154-platform
)
    target_compile_definitions(${_nrf802154_lib}
        PRIVATE
            ${NRF54_COMMON_DEFINES}
    )
    target_include_directories(${_nrf802154_lib}
        PRIVATE
            ${NRF54_SDK_INCLUDES}
            ${PROJECT_SOURCE_DIR}/src/nrf54l15
    )
endforeach()

foreach(_nrf802154_iface
    nrf-802154-driver-interface
    nrf-802154-common-interface
)
    target_compile_definitions(${_nrf802154_iface}
        INTERFACE
            ${NRF54_COMMON_DEFINES}
    )
    target_include_directories(${_nrf802154_iface}
        INTERFACE
            ${NRF54_SDK_INCLUDES}
            ${PROJECT_SOURCE_DIR}/src/nrf54l15
    )
endforeach()

# Step 3: SDK lib — minimal nrfx/MDK sources; extend as build reveals gaps.
set(NRF54_SDK_SOURCES
    ${NRF54_MDK_ROOT}/system_nrf54l.c
    ${NRF54_NRFX_ROOT}/drivers/src/nrfx_clock_hfclk.c
    ${NRF54_NRFX_ROOT}/drivers/src/nrfx_clock_lfclk.c
    ${NRF54_NRFX_ROOT}/drivers/src/nrfx_clock_xo.c
    ${NRF54_NRFX_ROOT}/drivers/src/nrfx_grtc.c
    ${NRF54_NRFX_ROOT}/helpers/nrfx_flag32_allocator.c
    ${NRF54_NRFX_ROOT}/helpers/nrfx_gppi_dppi.c
    ${NRF54_NRFX_ROOT}/helpers/nrfx_gppi_ppi.c
    ${NRF54_NRFX_ROOT}/bsp/stable/soc/interconnect/nrfx_gppi_d2ppi.c
)

add_library(nordicsemi-nrf54l15-sdk)
target_compile_definitions(nordicsemi-nrf54l15-sdk
    PRIVATE
        ${NRF54_COMMON_DEFINES}
)
target_include_directories(nordicsemi-nrf54l15-sdk
    PUBLIC
        ${NRF54_SDK_INCLUDES}
        ${PROJECT_SOURCE_DIR}/src/nrf54l15
)
target_sources(nordicsemi-nrf54l15-sdk
    PRIVATE
        ${NRF54_SDK_SOURCES}
)
target_link_libraries(nordicsemi-nrf54l15-sdk PRIVATE ot-config)

target_link_libraries(nrf-802154-platform PRIVATE nordicsemi-nrf54l15-sdk)

# Step 4: radio-driver wrapper — links Nordic CMake targets into ot-nrf54xx.
add_library(nordicsemi-nrf54l15-radio-driver INTERFACE)
target_compile_definitions(nordicsemi-nrf54l15-radio-driver
    INTERFACE
        ${NRF54_COMMON_DEFINES}
)
target_include_directories(nordicsemi-nrf54l15-radio-driver
    INTERFACE
        ${NRF54_RADIO_INCLUDES}
        ${NRF54_SDK_INCLUDES}
        ${OT_PUBLIC_INCLUDES}
        ${PROJECT_SOURCE_DIR}/src/nrf54l15
)
target_link_libraries(nordicsemi-nrf54l15-radio-driver
    INTERFACE
        nrf-802154-driver
        nrf-802154-sl
        nrf-802154-common
        nrf-802154-platform
        nrf-802154-driver-interface
        nrf-802154-common-interface
)
