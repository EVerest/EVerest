set(TEST_TARGET_NAME ${PROJECT_NAME}_tests)
add_executable(${TEST_TARGET_NAME})

add_dependencies(${TEST_TARGET_NAME} ${MODULE_NAME})
get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)

target_include_directories(${TEST_TARGET_NAME} PUBLIC
    ..
    ${GENERATED_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}/generated/modules/${MODULE_NAME}
)

target_sources(${TEST_TARGET_NAME}
    PRIVATE
    error_handling_tests.cpp
    everest_device_model_storage_test.cpp
    grid_support_conversions_test.cpp
    grid_support_state_test.cpp
    ocpp_conversions_test.cpp
    transaction_handler_tests.cpp
    ../conversions.cpp
    ../device_model/definitions.cpp
    ../device_model/everest_device_model_storage.cpp
    ../error_handling.cpp
    ../grid_support/grid_support_state.cpp
    ../transaction_handler.cpp
)

target_compile_definitions(${TEST_TARGET_NAME}
    PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
    everest::ocpp
    everest::ocpp_conversions
    everest::framework
    nlohmann_json::nlohmann_json
    GTest::gtest_main
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
