if(DISABLE_EDM)
    find_package(nlohmann_json REQUIRED)
endif()

set(TEST_TARGET_NAME ocpp_module_common_tests)
add_executable(${TEST_TARGET_NAME})

target_sources(${TEST_TARGET_NAME}
    PRIVATE
    error_handling_tests.cpp
    everest_device_model_storage_test.cpp
    grid_support_conversions_test.cpp
    transaction_handler_tests.cpp
)

target_compile_definitions(${TEST_TARGET_NAME}
    PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
    everest::ocpp_module_common
    nlohmann_json::nlohmann_json
    GTest::gtest_main
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
