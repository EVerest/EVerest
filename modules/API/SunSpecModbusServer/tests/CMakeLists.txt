set(TEST_TARGET_NAME ${PROJECT_NAME}_sunspec_modbus_server_tests)

add_executable(${TEST_TARGET_NAME}
    sunspec_map_tests.cpp
    ../lib/sunspec_map.cpp
)

target_include_directories(${TEST_TARGET_NAME}
    PRIVATE
        ../include
)

target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(${TEST_TARGET_NAME})

ev_register_test_target(${TEST_TARGET_NAME})
