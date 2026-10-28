set(COMM_CHECK_TEST_TARGET_NAME everest_api_module_helpers_comm_check_tests)

add_executable(${COMM_CHECK_TEST_TARGET_NAME}
  comm_check_handler.cpp
)

target_link_libraries(${COMM_CHECK_TEST_TARGET_NAME} PRIVATE
  GTest::gtest_main
  everest::everest_api_module_helpers
)

add_test(${COMM_CHECK_TEST_TARGET_NAME} ${COMM_CHECK_TEST_TARGET_NAME})

set(MQTT_PROVIDER_TEST_TARGET_NAME everest_api_module_helpers_mqtt_provider_tests)

add_executable(${MQTT_PROVIDER_TEST_TARGET_NAME}
  mqtt_provider_publish.cpp
)

target_link_libraries(${MQTT_PROVIDER_TEST_TARGET_NAME} PRIVATE
  GTest::gtest_main
  everest::everest_api_module_helpers
)

add_test(${MQTT_PROVIDER_TEST_TARGET_NAME} ${MQTT_PROVIDER_TEST_TARGET_NAME})
