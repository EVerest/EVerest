set(TEST_TARGET_NAME ${PROJECT_NAME}_phase_rotation_tests)

add_executable(${TEST_TARGET_NAME}
    phase_rotation_utils_test.cpp
)

target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
        GTest::gtest_main
        everest::phase_rotation
)

include(GoogleTest)
gtest_discover_tests(${TEST_TARGET_NAME})

ev_register_test_target(${TEST_TARGET_NAME})
