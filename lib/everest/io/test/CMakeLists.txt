add_executable(everest_io_tests
  endpoint_test.cpp
  udp_client_v6_test.cpp
  udp_unconnected_socket_test.cpp
  udp_dualstack_server_socket_test.cpp
  fd_event_client_async_test.cpp
)

target_link_libraries(everest_io_tests
  PRIVATE
        GTest::gtest_main
        everest::io
)

include(GoogleTest)
gtest_discover_tests(everest_io_tests TEST_PREFIX "io.")

add_executable(shm_coordinator_test test_shm_coordinator.cpp)

target_link_libraries(shm_coordinator_test
  PRIVATE
  everest::io
)

target_compile_features(shm_coordinator_test PUBLIC cxx_std_17)

add_test(shm_coordinator_test shm_coordinator_test)

add_executable(fd_event_handler_test test_fd_event_handler.cpp)

target_link_libraries(fd_event_handler_test
  PRIVATE
  everest::io
)

target_compile_features(fd_event_handler_test PUBLIC cxx_std_17)

add_test(fd_event_handler_test fd_event_handler_test)

add_executable(shm_segment_header_test test_shm_segment_header.cpp)

target_link_libraries(shm_segment_header_test
  PRIVATE
  everest::io
)

target_compile_features(shm_segment_header_test PUBLIC cxx_std_17)

add_test(shm_segment_header_test shm_segment_header_test)

add_executable(shm_topic_registry_test test_shm_topic_registry.cpp)

target_link_libraries(shm_topic_registry_test
  PRIVATE
  everest::io
)

target_compile_features(shm_topic_registry_test PUBLIC cxx_std_17)

add_test(shm_topic_registry_test shm_topic_registry_test)

add_executable(shm_topic_filter_test test_shm_topic_filter.cpp)

target_link_libraries(shm_topic_filter_test
  PRIVATE
  everest::io
)

target_compile_features(shm_topic_filter_test PUBLIC cxx_std_17)

add_test(shm_topic_filter_test shm_topic_filter_test)

add_executable(shm_topic_runtime_test test_shm_topic_runtime.cpp)

target_link_libraries(shm_topic_runtime_test
  PRIVATE
  everest::io
  nlohmann_json::nlohmann_json
)

target_compile_features(shm_topic_runtime_test PUBLIC cxx_std_17)

add_test(shm_topic_runtime_test shm_topic_runtime_test)

add_executable(shm_control_protocol_test test_shm_control_protocol.cpp)

target_link_libraries(shm_control_protocol_test
  PRIVATE
  everest::io
  nlohmann_json::nlohmann_json
)

target_compile_features(shm_control_protocol_test PUBLIC cxx_std_17)

add_test(shm_control_protocol_test shm_control_protocol_test)

add_executable(shm_control_server_test test_shm_control_server.cpp)

target_link_libraries(shm_control_server_test
  PRIVATE
  everest::io
  nlohmann_json::nlohmann_json
)

target_compile_features(shm_control_server_test PUBLIC cxx_std_17)

add_test(shm_control_server_test shm_control_server_test)

add_executable(shm_control_client_test test_shm_control_client.cpp)

target_link_libraries(shm_control_client_test
  PRIVATE
  everest::io
  nlohmann_json::nlohmann_json
)

target_compile_features(shm_control_client_test PUBLIC cxx_std_17)

add_test(shm_control_client_test shm_control_client_test)

add_executable(shm_control_integration_test test_shm_control_integration.cpp)

target_link_libraries(shm_control_integration_test
  PRIVATE
  everest::io
  nlohmann_json::nlohmann_json
)

target_compile_features(shm_control_integration_test PUBLIC cxx_std_17)

add_test(shm_control_integration_test shm_control_integration_test)

add_executable(shm_io_api_test test_shm_io_api.cpp)

target_link_libraries(shm_io_api_test
  PRIVATE
  everest::io
)

target_compile_features(shm_io_api_test PUBLIC cxx_std_17)

add_test(shm_io_api_test shm_io_api_test)

if(NOT TARGET ryml::ryml)
    find_package(ryml QUIET)
endif()

if(TARGET ryml::ryml)
    add_executable(shm_to_mqtt_bridge_config_test
      test_shm_to_mqtt_bridge_config.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/../examples/shm_to_mqtt_bridge/bridge_config.cpp
    )

    target_include_directories(shm_to_mqtt_bridge_config_test
      PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../examples/shm_to_mqtt_bridge
    )

    target_link_libraries(shm_to_mqtt_bridge_config_test
      PRIVATE
      everest::io
      ryml::ryml
    )

    target_compile_features(shm_to_mqtt_bridge_config_test PUBLIC cxx_std_17)

    add_test(shm_to_mqtt_bridge_config_test shm_to_mqtt_bridge_config_test)
endif()
