set(TEST_TARGET_NAME ${PROJECT_NAME}_ocpp_device_model_API_registration_collector_tests)

add_executable(${TEST_TARGET_NAME}
    registration_collector_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../registration_collector.cpp
)

add_dependencies(${TEST_TARGET_NAME} ${MODULE_NAME})

get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)

target_include_directories(${TEST_TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${GENERATED_INCLUDE_DIR}
)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    GTest::gtest_main
    everest::framework
    everest::everest_api_types
    everest::util
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
