set(TEST_TARGET_NAME ${PROJECT_NAME}_sunspec_solar_limit_tests)

add_executable(${TEST_TARGET_NAME}
    test_limit_policy.cpp
    test_sunspec_reader.cpp
    ../sunspec/limit_policy.cpp
    ../sunspec/sunspec_reader.cpp
)

target_include_directories(${TEST_TARGET_NAME}
    PRIVATE
        ..
)

target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(${TEST_TARGET_NAME})

ev_register_test_target(${TEST_TARGET_NAME})
