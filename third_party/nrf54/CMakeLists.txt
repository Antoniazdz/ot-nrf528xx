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

# nRF54L — hal_nordic (nrfx/MDK) + sdk-nrfxlib nrf_802154 (separate submodule).

cmake_policy(SET CMP0079 NEW)

set(NRF54_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(NORDIC_ROOT ${NRF54_ROOT}/nordic)

set(NRF54_NRFX_ROOT ${NORDIC_ROOT}/nrfx)
set(NRF54_MDK_ROOT ${NRF54_NRFX_ROOT}/bsp/stable/mdk)
set(NRF54_SOC_ROOT ${NRF54_NRFX_ROOT}/bsp/stable/soc)
set(NRF54_CONFIG_ROOT ${NRF54_ROOT}/config/nrf54l15)
set(NRF54_CMSIS_ROOT ${NRF54_ROOT}/cmsis)
set(NRF54_802154_ROOT ${NRF54_ROOT}/nrfxlib/nrf_802154)

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
    ${NRF54_802154_ROOT}/driver/src
    ${NRF54_802154_ROOT}/sl/include
    ${NRF54_802154_ROOT}/sl/sl_opensource/include
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
    -DNRF_CONFIG_CPU_FREQ_MHZ=128
)

set(NRF54_COMMON_DEFINES
    ${NRF54_FLAG}
    ${NRF54_CHIP_DEFINES}
    -DNRF_802154_PROJECT_CONFIG=\"platform-config.h\"
    -D__HEAP_SIZE=0
    -D__STACK_SIZE=2048
)

# Step 1: platform lib — required before Nordic driver subdirectory.
option(SL_OPENSOURCE "Use open-source 802.15.4 Service Layer (variant D POC)" OFF)
set(NRF54_SL_BINARY_PATH "${NRF54_802154_ROOT}/sl/sl/lib/nrf54l15_cpuapp/hard-float/libnrf-802154-sl.a"
    CACHE FILEPATH "Path to libnrf-802154-sl.a (nrf54l15_cpuapp hard-float)")

add_subdirectory(platform)

# Step 2: Nordic 802.15.4 driver via its own CMake.

if(NOT NRF_802154_PROJECT_CONFIG)
    set(NRF_802154_PROJECT_CONFIG platform-config.h)
endif()

include(${NRF54_ROOT}/cmake/nrf802154_driver.cmake)

# nrf_802154_swi.c omits #include "nrf_802154_peripherals.h" (Nordic upstream gap).
# Driver uses MAX()/MIN() — force-include shim (NCS/Zephyr provides nordic_common.h globally).
target_compile_options(nrf-802154-driver PRIVATE
    "SHELL:-include nrf_802154_peripherals.h"
    "SHELL:-include nordic_common_shim.h"
)

# Circular dependency between platform and sl (per Nordic docs).
target_link_libraries(nrf-802154-platform PRIVATE nrf-802154-sl)

# Propagate chip defines and include paths to Nordic targets.
foreach(_nrf802154_lib
    nrf-802154-driver
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
            ${NRF54_802154_ROOT}/sl/sl_opensource/include
            ${PROJECT_SOURCE_DIR}/src/nrf54l15
    )
    target_link_libraries(${_nrf802154_lib} PRIVATE ot-config)
endforeach()

if(SL_OPENSOURCE)
    target_compile_definitions(nrf-802154-sl PRIVATE ${NRF54_COMMON_DEFINES})
    target_include_directories(nrf-802154-sl
        PRIVATE
            ${NRF54_SDK_INCLUDES}
            ${NRF54_802154_ROOT}/sl/sl_opensource/include
            ${PROJECT_SOURCE_DIR}/src/nrf54l15
    )
    target_link_libraries(nrf-802154-sl PRIVATE ot-config)
else()
    target_compile_definitions(nrf-802154-sl INTERFACE ${NRF54_COMMON_DEFINES})
    target_include_directories(nrf-802154-sl
        INTERFACE
            ${NRF54_SDK_INCLUDES}
            ${NRF54_802154_ROOT}/sl/sl_opensource/include
            ${PROJECT_SOURCE_DIR}/src/nrf54l15
    )
    target_link_libraries(nrf-802154-sl INTERFACE ot-config)
endif()

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
    target_link_libraries(${_nrf802154_iface} INTERFACE ot-config)
endforeach()

# Step 3: SDK lib — minimal nrfx/MDK sources; extend as build reveals gaps.
set(NRF54_STARTUP_FILE ${NRF54_ROOT}/mdk/nrf54l/nrf54l15/gcc_startup_nrf54l15_application.S)

set(NRF54_SDK_SOURCES
    ${NRF54_STARTUP_FILE}
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
set_property(SOURCE ${NRF54_STARTUP_FILE} PROPERTY LANGUAGE C)

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
        nrf-802154-common
        nrf-802154-platform
        nrf-802154-sl
        nrf-802154-driver-interface
        nrf-802154-common-interface
)

# Variant C: MPSL replacement stubs — separate archive, whole-linked after SL .a
# so mpsl_cx / RAAL / timestamper objects are not dropped by static link order.
if(NOT SL_OPENSOURCE)
    add_library(nrf54-variant-c-stubs STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/nrf_raal_single_phy.c
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/mpsl_cx_stub.c
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/nrf_802154_platform_timestamper_stub.c
    )
    target_include_directories(nrf54-variant-c-stubs
        PRIVATE
            ${NRF54_SDK_INCLUDES}
            ${NRF54_802154_ROOT}/sl/include
            ${NRF54_802154_ROOT}/sl/include/platform
            ${NRF54_802154_ROOT}/common/include
            ${CMAKE_CURRENT_SOURCE_DIR}/platform
            ${PROJECT_SOURCE_DIR}/src/nrf54l15
    )
    target_compile_definitions(nrf54-variant-c-stubs PRIVATE ${NRF54_COMMON_DEFINES})
    target_link_libraries(nrf54-variant-c-stubs PRIVATE ot-config)

    target_link_libraries(nordicsemi-nrf54l15-radio-driver
        INTERFACE
            -Wl,--whole-archive
            nrf54-variant-c-stubs
            -Wl,--no-whole-archive
    )
endif()

# sym_* must be in the final executable (after SL .a scan), not in a static archive.
if(NOT SL_OPENSOURCE AND TARGET ot-rcp)
    target_sources(ot-rcp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/platform/sym_stub.c)
endif()
