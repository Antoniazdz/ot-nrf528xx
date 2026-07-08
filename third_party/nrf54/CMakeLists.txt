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

# nRF54L third-party tree (SDK + 802.15.4 radio driver from nRF Connect SDK).
# Vendor files into sdk/ and radio/, then extend the source lists below.

set(NRF54_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(NRF54_SDK_INCLUDES
    ${NRF54_ROOT}/sdk
    ${NRF54_ROOT}/sdk/cmsis
    ${NRF54_ROOT}/sdk/nrfx
    ${NRF54_ROOT}/sdk/nrfx/hal
    ${NRF54_ROOT}/sdk/nrfx/drivers/include
    ${NRF54_ROOT}/sdk/nrfx/mdk
    ${NRF54_ROOT}/sdk/nrfx/soc
    ${NRF54_ROOT}/config/nrf54l15
)

set(NRF54_RADIO_INCLUDES
    ${NRF54_ROOT}/radio
    ${PROJECT_SOURCE_DIR}/openthread/include
    ${PROJECT_SOURCE_DIR}/openthread/src
    ${PROJECT_SOURCE_DIR}/openthread/src/core
)

set(NRF54_FLAG
    -DCONFIG_GPIO_AS_PINRESET
    -DENABLE_FEM=1
    -DUSE_APP_CONFIG=1
)

# Replace placeholder.c entries as files are vendored from NCS.
set(NRF54_SDK_SOURCES
    sdk/placeholder.c
    # sdk/nrfx/mdk/gcc_startup_nrf54l15.S
    # sdk/nrfx/mdk/system_nrf54l15.c
)

set(NRF54_RADIO_SOURCES
    radio/placeholder.c
)

add_library(nordicsemi-nrf54l15-sdk)
target_compile_definitions(nordicsemi-nrf54l15-sdk
    PRIVATE
        ${NRF54_FLAG}
        -DNRF54L15_XXAA
        -DNRF_802154_PROJECT_CONFIG=\"platform-config.h\"
        -D__HEAP_SIZE=0
        -D__STACK_SIZE=2048
)
target_include_directories(nordicsemi-nrf54l15-sdk
    PUBLIC
        ${NRF54_SDK_INCLUDES}
        ${PROJECT_SOURCE_DIR}/src/nrf54l15
)
if(EXISTS ${NRF54_ROOT}/sdk/nrfx/mdk/gcc_startup_nrf54l15.S)
    set_property(SOURCE sdk/nrfx/mdk/gcc_startup_nrf54l15.S PROPERTY LANGUAGE C)
endif()
target_sources(nordicsemi-nrf54l15-sdk
    PRIVATE
        ${NRF54_SDK_SOURCES}
)
target_link_libraries(nordicsemi-nrf54l15-sdk PRIVATE ot-config)

add_library(nordicsemi-nrf54l15-radio-driver)
target_compile_definitions(nordicsemi-nrf54l15-radio-driver
    PRIVATE
        ${NRF54_FLAG}
        -DNRF54L15_XXAA
        -DNRF_802154_PROJECT_CONFIG=\"platform-config.h\"
        -D__HEAP_SIZE=0
        -D__STACK_SIZE=2048
        -DRAAL_SINGLE_PHY=1
)
target_include_directories(nordicsemi-nrf54l15-radio-driver
    PUBLIC
        ${NRF54_RADIO_INCLUDES}
        ${NRF54_SDK_INCLUDES}
        ${OT_PUBLIC_INCLUDES}
        ${PROJECT_SOURCE_DIR}/src/nrf54l15
)
target_sources(nordicsemi-nrf54l15-radio-driver
    PRIVATE
        ${NRF54_RADIO_SOURCES}
)
target_link_libraries(nordicsemi-nrf54l15-radio-driver PRIVATE ot-config)
