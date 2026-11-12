set(TEST_TARGET_NAME ocpp_conversions_tests)
add_executable(${TEST_TARGET_NAME} ocpp_conversions_test.cpp)

target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
    everest::ocpp_conversions
    everest::ocpp
    everest::framework
    GTest::gtest_main
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
