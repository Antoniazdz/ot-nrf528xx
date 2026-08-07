# Variant C spike — CMake fragment (included from third_party/nrf54/CMakeLists.txt)

set(NRF54_SPIKE_STUB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/spike_c/stubs)

set(NRF54_SPIKE_STUB_SOURCES
    ${NRF54_SPIKE_STUB_DIR}/nrf_raal_single_phy.c
    ${NRF54_SPIKE_STUB_DIR}/mpsl_cx_stub.c
    ${NRF54_SPIKE_STUB_DIR}/wifi_coex_stub.c
    ${NRF54_SPIKE_STUB_DIR}/timestamper_stub.c
    ${NRF54_802154_ROOT}/sl/sl_opensource/src/nrf_802154_sl_crit_sect_if.c
)

add_library(nrf54-spike-c-stubs STATIC ${NRF54_SPIKE_STUB_SOURCES})

target_include_directories(nrf54-spike-c-stubs
    PUBLIC
        ${NRF54_802154_ROOT}/sl/sl_opensource/include
        ${NRF54_802154_ROOT}/common/include
        ${NRF54_802154_ROOT}/sl/include
        ${NRF54_802154_ROOT}/sl/sl_opensource/include
        ${NRF54_802154_ROOT}/driver/include
        ${NRF54_SDK_INCLUDES}
        ${PROJECT_SOURCE_DIR}/src/nrf54l15
)

target_compile_definitions(nrf54-spike-c-stubs
    PRIVATE
        ${NRF54_COMMON_DEFINES}
)

target_link_libraries(nrf54-spike-c-stubs PRIVATE ot-config)

set(NRF54_MDK_LINKER_DIR "${PROJECT_SOURCE_DIR}/third_party/nrf54/mdk")
set(NRF54_SPIKE_LD_FILE "${NRF54_MDK_LINKER_DIR}/nrf54l/nrf54l15/nrf54l15_xxaa_application.ld")
set(NRF54_SPIKE_LD_COMMON "${NRF54_MDK_LINKER_DIR}/common")

add_executable(radio-spike-sl-binary-nrf54l15
    ${CMAKE_CURRENT_SOURCE_DIR}/spike_c/main_spike.c
    # Linked in the executable (not stubs .a): SL binary is the only sym_* user and
    # is pulled in after the archive scan — archive link order would drop sym_stub.o.
    ${NRF54_SPIKE_STUB_DIR}/sym_stub.c
)

target_compile_definitions(radio-spike-sl-binary-nrf54l15
    PRIVATE
        ${NRF54_COMMON_DEFINES}
)

target_include_directories(radio-spike-sl-binary-nrf54l15
    PRIVATE
        ${NRF54_SDK_INCLUDES}
        ${NRF54_RADIO_INCLUDES}
        ${PROJECT_SOURCE_DIR}/src/nrf54l15
        ${NRF54_802154_ROOT}/sl/include
        ${PROJECT_SOURCE_DIR}/third_party/jlink/SEGGER_RTT_V640/RTT
)

target_link_libraries(radio-spike-sl-binary-nrf54l15
    PRIVATE
        nrf54-spike-c-stubs
        nordicsemi-nrf54l15-radio-driver
        nordicsemi-nrf54l15-sdk
        jlinkrtt
        ot-config
        -L${NRF54_SPIKE_LD_COMMON}
        -T${NRF54_SPIKE_LD_FILE}
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/radio-spike-sl-binary-nrf54l15.map
)

set_target_properties(radio-spike-sl-binary-nrf54l15 PROPERTIES LINKER_LANGUAGE C)

message(STATUS "Variant C spike target: radio-spike-sl-binary-nrf54l15")
