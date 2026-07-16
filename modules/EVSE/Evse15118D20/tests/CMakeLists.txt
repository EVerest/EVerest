set(TEST_TARGET_NAME ${PROJECT_NAME}_Evse15118D20_tests)

add_executable(${TEST_TARGET_NAME})

add_dependencies(${TEST_TARGET_NAME} ${MODULE_NAME})

get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)

target_include_directories(${TEST_TARGET_NAME} PRIVATE
    ../charger
    ${GENERATED_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}/generated/modules/${MODULE_NAME}
)

target_sources(${TEST_TARGET_NAME} PRIVATE
    DerSetupTest.cpp
    DerChargingParametersTest.cpp
    DerControlsTest.cpp
    DerRelayTest.cpp
    GridEventTest.cpp
    ../charger/der_setup.cpp
    ../charger/der_relay.cpp
    ../charger/grid_event.cpp
)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    GTest::gmock
    GTest::gtest_main
    everest::log
    everest::framework
    iso15118::iso15118
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
