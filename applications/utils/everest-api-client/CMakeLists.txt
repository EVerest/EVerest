# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
# Generates a Python client wheel for each EVerest API module from its
# AsyncAPI spec (docs/source/reference/EVerest_API) using the in-tree Python
# generator everest-api-client-gen, builds the hand-written spyclient_api
# wheel, and installs the interactive cmd2 shell (api_client.py) that ties them
# together. No npm/asyncapi toolchain is involved.
#
# Enable with -DEVEREST_BUILD_API_CLIENTS=ON.

set(EVEREST_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(EVEREST_API_SPECS_DIR ${EVEREST_ROOT_DIR}/docs/source/reference/EVerest_API)

# provides EVEREST_API_CLIENT_GEN (the generator) and
# EVEREST_API_CLIENT_GEN_TEMPLATES (its template files, used as build deps)
include(${EVEREST_ROOT_DIR}/cmake/everest-api-client-gen.cmake)
setup_everest_api_client_gen()

add_custom_target(everest_api_clients ALL)

# Generate one client package from a spec and wire up its wheel + install
# targets. Replaces the former npm asyncapi-cli invocation.
function(add_api_client)
    set(oneValueArgs API_PATH NAME_CAMEL_CASE NAME_SNAKE_CASE INTERFACE_PREFIX)
    cmake_parse_arguments(ARG "" "${oneValueArgs}" "" ${ARGN})

    set(OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/async_python_client_${ARG_NAME_SNAKE_CASE})
    set(DONE_FILE ${CMAKE_CURRENT_BINARY_DIR}/generated/async_python_client_${ARG_NAME_SNAKE_CASE}_done)

    add_custom_command(
        OUTPUT ${DONE_FILE}
        COMMAND ${EVEREST_API_CLIENT_GEN}
            --spec ${ARG_API_PATH}
            --out ${OUTPUT_DIR}
            --name-camel-case ${ARG_NAME_CAMEL_CASE}
            --name-snake-case ${ARG_NAME_SNAKE_CASE}
            --interface-prefix ${ARG_INTERFACE_PREFIX}
            --server default
        COMMAND ${CMAKE_COMMAND} -E touch ${DONE_FILE}
        DEPENDS ${ARG_API_PATH} ${EVEREST_API_CLIENT_GEN_TEMPLATES}
        COMMENT "Generating async python client for ${ARG_NAME_CAMEL_CASE}"
    )
    add_custom_target(async_python_client_${ARG_NAME_SNAKE_CASE} DEPENDS ${DONE_FILE})

    ev_create_python_wheel_targets(
        PACKAGE_NAME async_python_client_${ARG_NAME_SNAKE_CASE}
        PACKAGE_SOURCE_DIRECTORY ${OUTPUT_DIR}
        DEPENDS async_python_client_${ARG_NAME_SNAKE_CASE}
    )
    ev_create_pip_install_targets(
        PACKAGE_NAME async_python_client_${ARG_NAME_SNAKE_CASE}
        PACKAGE_SOURCE_DIRECTORY ${OUTPUT_DIR}
        DIST_DEPENDS async_python_client_${ARG_NAME_SNAKE_CASE}
        LOCAL_DEPENDS async_python_client_${ARG_NAME_SNAKE_CASE}
    )
    add_dependencies(everest_api_clients
        async_python_client_${ARG_NAME_SNAKE_CASE}_install_wheel
    )
endfunction()

# One entry per EVerest API module spec: spec file name (without .yaml),
# camel case name, snake case name and the cmd2 command prefix. The prefix
# only has to be unique within the CLI; it does not appear in MQTT topics.
# Keep this list in sync with GENERATED_API_CLIENTS in api_client.py.
set(SET01 "auth_consumer_API" "AuthConsumerAPI" "auth_consumer_api" "tc")
set(SET02 "auth_token_provider_API" "AuthTokenProviderAPI" "auth_token_provider_api" "tp")
set(SET03 "auth_token_validator_API" "AuthTokenValidatorAPI" "auth_token_validator_api" "tv")
set(SET04 "dc_external_derate_consumer_API" "DcExternalDerateConsumerAPI" "dc_external_derate_consumer_api" "ded")
set(SET05 "display_message_API" "DisplayMessageAPI" "display_message_api" "dm")
set(SET06 "error_history_consumer_API" "ErrorHistoryConsumerAPI" "error_history_consumer_api" "eh")
set(SET07 "ev_board_support_API" "EvBspAPI" "ev_bsp_api" "evbsp")
set(SET08 "evse_board_support_API" "EvseBspAPI" "evse_bsp_api" "bsp")
set(SET09 "evse_manager_consumer_API" "EvseManagerConsumerAPI" "evse_manager_consumer_api" "em")
set(SET10 "evse_security_consumer_API" "EvseSecurityConsumerAPI" "evse_security_consumer_api" "sec")
set(SET11 "external_energy_limits_consumer_API" "ExternalEnergyLimitsConsumerAPI" "external_energy_limits_consumer_api" "eel")
set(SET12 "generic_error_raiser_API" "GenericErrorRaiserAPI" "generic_error_raiser_api" "err")
set(SET13 "isolation_monitor_API" "IsolationMonitorAPI" "isolation_monitor_api" "im")
set(SET14 "ocpp_consumer_API" "OcppConsumerAPI" "ocpp_consumer_api" "oc")
set(SET15 "over_voltage_monitor_API" "OverVoltageMonitorAPI" "over_voltage_monitor_api" "ovm")
set(SET16 "power_supply_DC_API" "PowerSupplyDCAPI" "power_supply_dc_api" "ps")
set(SET17 "powermeter_API" "PowermeterAPI" "powermeter_api" "pm")
set(SET18 "session_cost_API" "SessionCostAPI" "session_cost_api" "sc")
set(SET19 "session_cost_consumer_API" "SessionCostConsumerAPI" "session_cost_consumer_api" "scc")
set(SET20 "slac_API" "SlacAPI" "slac_api" "slac")
set(SET21 "system_API" "SystemAPI" "system_api" "sys")

set(API_SPECS
    "SET01" "SET02" "SET03" "SET04" "SET05" "SET06" "SET07" "SET08" "SET09"
    "SET10" "SET11" "SET12" "SET13" "SET14" "SET15" "SET16" "SET17" "SET18"
    "SET19" "SET20" "SET21"
)

foreach(API_SPEC IN LISTS API_SPECS)
    set(API_SPEC_PARAMS ${${API_SPEC}})
    list(GET API_SPEC_PARAMS 0 API_SPEC_NAME)
    list(GET API_SPEC_PARAMS 1 API_NAME_CAMEL_CASE)
    list(GET API_SPEC_PARAMS 2 API_NAME_SNAKE_CASE)
    list(GET API_SPEC_PARAMS 3 API_INTERFACE_PREFIX)
    message(STATUS "Generating API client for: ${API_SPEC_NAME} (prefix: ${API_INTERFACE_PREFIX})")
    add_api_client(
        API_PATH ${EVEREST_API_SPECS_DIR}/${API_SPEC_NAME}.yaml
        NAME_CAMEL_CASE ${API_NAME_CAMEL_CASE}
        NAME_SNAKE_CASE ${API_NAME_SNAKE_CASE}
        INTERFACE_PREFIX ${API_INTERFACE_PREFIX}
    )
endforeach()

# hand-written spy client wheel
ev_create_python_wheel_targets(
    PACKAGE_NAME async_python_client_spyclient_api
    PACKAGE_SOURCE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/spyclient_api
)
ev_create_pip_install_targets(
    PACKAGE_NAME async_python_client_spyclient_api
    PACKAGE_SOURCE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/spyclient_api
)
add_dependencies(everest_api_clients
    async_python_client_spyclient_api_install_wheel
)

include(GNUInstallDirs)
install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/api_client.py
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ
)
