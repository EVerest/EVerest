set(COMM_CHECK_TEST_TARGET_NAME everest_api_module_helpers_comm_check_tests)

add_executable(${COMM_CHECK_TEST_TARGET_NAME}
  comm_check_handler.cpp
)

target_link_libraries(${COMM_CHECK_TEST_TARGET_NAME} PRIVATE
  GTest::gtest_main
  everest::everest_api_module_helpers
)

add_test(${COMM_CHECK_TEST_TARGET_NAME} ${COMM_CHECK_TEST_TARGET_NAME})
