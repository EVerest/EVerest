set(TEST_TARGET_NAME ${PROJECT_NAME}_iso15118_conversions_tests)

add_executable(${TEST_TARGET_NAME}
    v2g_message_id_test.cpp
)

target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
        GTest::gtest_main
        everest::iso15118_conversions
)

include(GoogleTest)
gtest_discover_tests(${TEST_TARGET_NAME})

ev_register_test_target(${TEST_TARGET_NAME})
