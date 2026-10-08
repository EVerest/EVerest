if(DISABLE_EDM)
    find_package(nlohmann_json REQUIRED)
endif()

set(TEST_TARGET_NAME ocpp_module_common_tests)
add_executable(${TEST_TARGET_NAME})

target_sources(${TEST_TARGET_NAME}
    PRIVATE
    composed_device_model_storage_test.cpp
    data_transfer_conversions_test.cpp
    error_handling_tests.cpp
    everest_device_model_storage_test.cpp
    grid_support_conversions_test.cpp
    network_conversions_test.cpp
    transaction_handler_tests.cpp
)

# Relative to the WORKING_DIRECTORY, so the compile flags don't differ between build trees.
file(RELATIVE_PATH TEST_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/data)
file(RELATIVE_PATH DEVICE_MODEL_MIGRATIONS_DIR ${CMAKE_CURRENT_BINARY_DIR}
    ${PROJECT_SOURCE_DIR}/lib/everest/ocpp/config/common/device_model_migrations)
target_compile_definitions(${TEST_TARGET_NAME}
    PRIVATE
    TEST_DATA_DIR="${TEST_DATA_DIR}"
    DEVICE_MODEL_MIGRATIONS_DIR="${DEVICE_MODEL_MIGRATIONS_DIR}"
)

target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
    everest::ocpp_module_common
    nlohmann_json::nlohmann_json
    GTest::gtest_main
)

add_test(NAME ${TEST_TARGET_NAME} COMMAND ${TEST_TARGET_NAME} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
ev_register_test_target(${TEST_TARGET_NAME})
