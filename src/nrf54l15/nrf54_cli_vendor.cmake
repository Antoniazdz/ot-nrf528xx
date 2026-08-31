#
# nRF54 platform CLI: nrf54stats
#

get_filename_component(NRF54_CLI_VENDOR_DIR "${OT_CLI_VENDOR_EXTENSION}" DIRECTORY)

add_library(nrf54-cli-vendor ${NRF54_CLI_VENDOR_DIR}/nrf54_cli_vendor.c)

target_compile_definitions(nrf54-cli-vendor PRIVATE NRF54_DEBUG_STATS=1)

target_link_libraries(nrf54-cli-vendor PRIVATE ot-config openthread-nrf54l15)

target_include_directories(nrf54-cli-vendor PRIVATE
    ${NRF54_CLI_VENDOR_DIR}
    ${PROJECT_SOURCE_DIR}/third_party/nrf54/platform
)

target_compile_definitions(ot-config INTERFACE
    "OPENTHREAD_CONFIG_CLI_VENDOR_COMMANDS_ENABLE=1"
    "OPENTHREAD_CONFIG_CLI_MAX_USER_CMD_ENTRIES=2"
)

set(OT_CLI_VENDOR_TARGET nrf54-cli-vendor)
