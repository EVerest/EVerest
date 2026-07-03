set(TEST_TARGET_NAME ${PROJECT_NAME}_OCPPmulti_tests)
set(TESTS_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/tests/include")

add_executable(${TEST_TARGET_NAME})

add_dependencies(${TEST_TARGET_NAME} ${MODULE_NAME})
get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)

target_include_directories(${TEST_TARGET_NAME} PUBLIC
    .
    ..
    ${TESTS_INCLUDE_DIR}
    ${GENERATED_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}/generated/modules/${MODULE_NAME}
    "$<TARGET_PROPERTY:everest::framework,INCLUDE_DIRECTORIES>"
)

target_sources(${TEST_TARGET_NAME}
    PRIVATE

    # provided interfaces tests
    auth_provider_tests.cpp
    auth_validator_tests.cpp
    data_transfer_tests.cpp
    ocpp_generic_tests.cpp
    session_cost_tests.cpp

    # required interfaces tests
    authIntf_tests.cpp
    charger_informationIntf_tests.cpp
    display_messageIntf_tests.cpp
    evse_managerIntf_tests.cpp
    # evse_securityIntf_tests.cpp
    external_energy_limitsIntf_tests.cpp
    iso15118_extensionsIntf_tests.cpp
    ocpp_data_transferIntf_tests.cpp
    reservationIntf_tests.cpp
    systemIntf_tests.cpp

    # general
    generic_ocpp_tests.cpp
    main.cpp
    v16_chargepoint_tests.cpp

    everest/logging.cpp
    stubs/chargepoint_stub.cpp
    stubs/interfaces_stub.cpp

    ../generic_ocpp.cpp
    ../v16_chargepoint.cpp
    ../charge_point_config_factory_v16.cpp
)

target_compile_definitions(${TEST_TARGET_NAME}
    PRIVATE
    UNIT_TESTS
)

target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
    everest::external_energy_limits
    everest::ocpp
    everest::ocpp_conversions
    everest::ocpp_evse_security
    everest::ocpp_module_common
    GTest::gmock
    GTest::gtest
)

# add needed config for the unit tests
set(TEST_FILES_SQL
    1_up-initial.sql
    2_down-variable_source.sql
    2_up-variable_source.sql
    3_down-variable_required.sql
    3_up-variable_required.sql
)
set(TEST_FILES_JSON
    default_store.json
)

set(TEST_FILES_SQL_DEST OCPP201/device_model_migrations)
set(TEST_FILES_JSON_DEST ${CMAKE_CURRENT_BINARY_DIR})

foreach(X ${TEST_FILES_SQL})
    list(APPEND TEST_FILES_SQL_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/config/${X})
    list(APPEND TEST_FILES_SQL_OUTPUT ${TEST_FILES_SQL_DEST}/${X})
endforeach()

foreach(X ${TEST_FILES_JSON})
    list(APPEND TEST_FILES_JSON_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/config/${X})
    list(APPEND TEST_FILES_JSON_OUTPUT ${X})
endforeach()

set(TEST_FILES_OUTPUT ${TEST_FILES_SQL_OUTPUT} ${TEST_FILES_JSON_OUTPUT})

add_custom_command(
    OUTPUT ${TEST_FILES_OUTPUT}
    DEPENDS ${TEST_FILES_SQL_INPUT} ${TEST_FILES_JSON_INPUT}

    COMMAND mkdir -p ${TEST_FILES_SQL_DEST} OCPP
    COMMAND mkdir -p OCPP201/dm_config/standardized
    COMMAND cp ${TEST_FILES_SQL_INPUT} ${TEST_FILES_SQL_DEST}/
    COMMAND cp ${TEST_FILES_JSON_INPUT} ${TEST_FILES_JSON_DEST}/
)

add_custom_target(${TEST_TARGET_NAME}_CONFIG
    DEPENDS ${TEST_FILES_OUTPUT}
)

add_dependencies(${TEST_TARGET_NAME} ${TEST_TARGET_NAME}_CONFIG)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
