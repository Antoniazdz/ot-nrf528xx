#
#  Copyright (c) 2021, The OpenThread Authors.
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

# Minimal RCP build: single-phy radio, UART transport, no SoftDevice/bootloader.

set(NRF_PLATFORM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/nrf54l15)

set(NRF_COMM_SOURCES
    ${NRF_PLATFORM_DIR}/alarm_nrf54.c
    ${NRF_PLATFORM_DIR}/crypto_nrf54.c
    ${NRF_PLATFORM_DIR}/diag.c
    ${NRF_PLATFORM_DIR}/entropy_nrf54.c
    ${NRF_PLATFORM_DIR}/fem_nrf54.c
    ${NRF_PLATFORM_DIR}/flash.c
    ${NRF_PLATFORM_DIR}/flash_nosd.c
    ${NRF_PLATFORM_DIR}/logging.c
    ${NRF_PLATFORM_DIR}/misc_nrf54.c
    ${NRF_PLATFORM_DIR}/radio_nrf54.c
    ${NRF_PLATFORM_DIR}/system_nrf54.c
    ${NRF_PLATFORM_DIR}/temp_nrf54.c
)

set(NRF_TRANSPORT_SOURCES
    ${NRF_PLATFORM_DIR}/transport/transport.c
    ${NRF_PLATFORM_DIR}/uart_nrf54.c
)

set(NRF54L15_3RD_LIBS
    nordicsemi-nrf54l15-radio-driver
    nordicsemi-nrf54l15-sdk
    jlinkrtt
)

set(NRF_INCLUDES
    ${NRF_PLATFORM_DIR}
    ${PROJECT_SOURCE_DIR}/openthread/examples/platforms
)

set(NRF54_MDK_LINKER_DIR "${PROJECT_SOURCE_DIR}/third_party/nrf54/mdk")
set(LD_FILE "${NRF54_MDK_LINKER_DIR}/nrf54l/nrf54l15/nrf54l15_xxaa_application.ld")
set(LD_COMMON_DIR "${NRF54_MDK_LINKER_DIR}/common")

set(COMM_FLAGS
    -DCONFIG_GPIO_AS_PINRESET
    -DNRF54L15_XXAA
    -DNRF_APPLICATION
    -DUSE_APP_CONFIG=1
    -Wno-unused-parameter
    -Wno-expansion-to-defined
)

list(APPEND OT_PUBLIC_INCLUDES
    "${PROJECT_SOURCE_DIR}/third_party/nrf54/cmsis"
    "${PROJECT_SOURCE_DIR}/third_party/nrf54/config/nrf54l15"
    "${PROJECT_SOURCE_DIR}/third_party/nrf54/nordic/nrfx/bsp/stable/mdk"
    "${PROJECT_SOURCE_DIR}/third_party/nrf54/nordic/nrfx/bsp/stable"
    "${PROJECT_SOURCE_DIR}/third_party/nrf54/nordic/nrfx/bsp/stable/templates"
    "${PROJECT_SOURCE_DIR}/third_party/nrf54/nordic/nrfx"
    "${PROJECT_SOURCE_DIR}/third_party/nrf54/nrfxlib/nrf_802154/driver/src"
    "${PROJECT_SOURCE_DIR}/third_party/nrf54/nrfxlib/nrf_802154/common/include"
    "${PROJECT_SOURCE_DIR}/third_party/nrf54/nrfxlib/nrf_802154/sl/include/platform"
)

set(OT_PLATFORM_DEFINES ${OT_PLATFORM_DEFINES} PARENT_SCOPE)
target_compile_definitions(ot-config INTERFACE
    "MBEDTLS_USER_CONFIG_FILE=\"nrf54l15-mbedtls-config.h\""
    PLATFORM_OPENTHREAD_VANILLA=1 # CSL-F4.1: exposes define to main.c for nrf54ProcessMainLoop
    NRF54_DEBUG_STATS=1
)
target_include_directories(ot-config INTERFACE
    "${PROJECT_SOURCE_DIR}/third_party/nrf54/platform"
)
set(OT_PUBLIC_INCLUDES ${OT_PUBLIC_INCLUDES} PARENT_SCOPE)

if(OT_CFLAGS MATCHES "-pedantic-errors")
    string(REPLACE "-pedantic-errors" "" OT_CFLAGS "${OT_CFLAGS}")
endif()

set(OT_UART_BAUDRATE 1000000 CACHE STRING "UART baud rate for nRF54L15 RCP (Spinel).
Must match a NRF_UARTE_BAUDRATE_* symbol from transport-config.h / nrfx HAL.")
add_definitions(-DUART_BAUDRATE=NRF_UARTE_BAUDRATE_${OT_UART_BAUDRATE})

add_library(openthread-nrf54l15
    ${NRF_COMM_SOURCES}
    $<TARGET_OBJECTS:openthread-platform-utils>
)

add_library(openthread-nrf54l15-transport
    ${NRF_TRANSPORT_SOURCES}
)

add_library(openthread-nrf54l15-sdk
    ${NRF_COMM_SOURCES}
    $<TARGET_OBJECTS:openthread-platform-utils>
)

set_target_properties(openthread-nrf54l15 openthread-nrf54l15-transport openthread-nrf54l15-sdk
    PROPERTIES
        C_STANDARD 99
        CXX_STANDARD 11
)

target_link_libraries(openthread-nrf54l15
    PUBLIC
        ${OT_MBEDTLS}
        ${NRF54L15_3RD_LIBS}
        -L${LD_COMMON_DIR}
        -T${LD_FILE}
        -Wl,--gc-sections
        -Wl,-Map=$<TARGET_PROPERTY:NAME>.map
    PRIVATE
        ot-config
)

target_link_libraries(openthread-nrf54l15-transport
    PUBLIC
        ${OT_MBEDTLS}
        -L${LD_COMMON_DIR}
        -T${LD_FILE}
        -Wl,--gc-sections
        -Wl,-Map=$<TARGET_PROPERTY:NAME>.map
    PRIVATE
        nordicsemi-nrf54l15-sdk
        ot-config
)

target_link_libraries(openthread-nrf54l15-sdk
    PUBLIC
        ${OT_MBEDTLS}
        ${NRF54L15_3RD_LIBS}
        -L${LD_COMMON_DIR}
        -T${LD_FILE}
        -Wl,--gc-sections
        -Wl,-Map=$<TARGET_PROPERTY:NAME>.map
    PRIVATE
        ot-config
)

target_compile_definitions(openthread-nrf54l15
    PUBLIC
        ${OT_PLATFORM_DEFINES}
)

target_compile_definitions(openthread-nrf54l15-transport
    PUBLIC
        ${OT_PLATFORM_DEFINES}
)

target_compile_definitions(openthread-nrf54l15-sdk
    PUBLIC
        ${OT_PLATFORM_DEFINES}
)

target_compile_options(openthread-nrf54l15
    PRIVATE
        ${OT_CFLAGS}
        ${COMM_FLAGS}
        -DRAAL_SINGLE_PHY=1
        -DPLATFORM_OPENTHREAD_VANILLA
)

target_compile_options(openthread-nrf54l15-transport
    PRIVATE
        ${OT_CFLAGS}
        ${COMM_FLAGS}
)

target_compile_options(openthread-nrf54l15-sdk
    PRIVATE
        ${OT_CFLAGS}
        ${COMM_FLAGS}
        -DRAAL_SINGLE_PHY=1
        -DPLATFORM_OPENTHREAD_VANILLA
)

target_include_directories(openthread-nrf54l15
    PRIVATE
        ${NRF_INCLUDES}
        ${OT_PUBLIC_INCLUDES}
)

target_include_directories(openthread-nrf54l15-transport
    PRIVATE
        ${NRF_INCLUDES}
        ${OT_PUBLIC_INCLUDES}
)

target_include_directories(openthread-nrf54l15-sdk
    PRIVATE
        ${NRF_INCLUDES}
        ${OT_PUBLIC_INCLUDES}
)

target_include_directories(ot-config INTERFACE ${OT_PUBLIC_INCLUDES})
target_compile_definitions(ot-config INTERFACE ${OT_PLATFORM_DEFINES})
