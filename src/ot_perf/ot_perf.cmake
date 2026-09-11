#
#  Copyright (c) 2026, The OpenThread Authors.
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

# OpenThread CLI vendor extension providing the `perf` UDP throughput command.
#
# openthread/src/cli/CMakeLists.txt includes this file and links OT_CLI_VENDOR_TARGET into the
# CLI apps, both for this bare-metal port and for a Zephyr/NCS build:
#
#   bare metal:  ./script/build nrf54l15 UART_trans \
#                    -DOT_CLI_VENDOR_EXTENSION=${PWD}/src/ot_perf/ot_perf.cmake
#   NCS:         CONFIG_OPENTHREAD_CLI_VENDOR_EXTENSION="<abs path>/src/ot_perf/ot_perf.cmake"

# Entry 0 belongs to the app itself; the vendor hook is only invoked when a second slot exists.
target_compile_definitions(ot-config INTERFACE
    "OPENTHREAD_CONFIG_CLI_MAX_USER_CMD_ENTRIES=2"
)

add_library(ot-perf
    ${CMAKE_CURRENT_LIST_DIR}/ot_perf.c
    ${CMAKE_CURRENT_LIST_DIR}/ot_perf_timing.c
    ${CMAKE_CURRENT_LIST_DIR}/ot_perf_drain_port.c
)

set_target_properties(ot-perf PROPERTIES C_STANDARD 99)

# ot-config carries the OpenThread public includes and, in a Zephyr build, the Zephyr include
# directories, compile definitions and options as well.
target_link_libraries(ot-perf PRIVATE ot-config)

target_include_directories(ot-perf PRIVATE ${OT_PUBLIC_INCLUDES})

# `perf cca` needs nrf_802154_types.h (bare metal) or nrf_802154.h (Zephyr/NCS) for the driver
# stat counters. Take the header from whichever nrf_802154 the image actually links, otherwise
# nrf_802154_stat_counters_t could be parsed against the wrong version's layout: the nrfxlib
# module in a Zephyr build, the in-tree copy here on bare metal.
if(DEFINED ZEPHYR_NRFXLIB_MODULE_DIR)
    set(OT_PERF_NRF_802154_INCLUDE "${ZEPHYR_NRFXLIB_MODULE_DIR}/nrf_802154/common/include")
else()
    set(OT_PERF_NRF_802154_INCLUDE
        "${CMAKE_CURRENT_LIST_DIR}/../../third_party/nrf54/nrfxlib/nrf_802154/common/include")
endif()

if(EXISTS "${OT_PERF_NRF_802154_INCLUDE}")
    target_include_directories(ot-perf PRIVATE "${OT_PERF_NRF_802154_INCLUDE}")
else()
    message(WARNING "ot_perf: nrf_802154 headers not found, `perf cca` will report no counters")
endif()

if(TARGET zephyr_generated_headers)
    add_dependencies(ot-perf zephyr_generated_headers)
endif()

# Static-library link options do not reach zephyr.elf; use global Zephyr LDFLAGS.
if(DEFINED ZEPHYR_NRFXLIB_MODULE_DIR)
    zephyr_ld_options(
        -Wl,--wrap=otPlatRadioTxDone
        -Wl,--wrap=otPlatRadioTransmit
        -Wl,--wrap=otSysProcessDrivers
    )
endif()

set(OT_CLI_VENDOR_TARGET ot-perf)
