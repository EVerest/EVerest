add_executable(everest_io_tests
  endpoint_test.cpp
  udp_client_v6_test.cpp
  udp_unconnected_socket_test.cpp
  udp_dualstack_server_socket_test.cpp
)

target_link_libraries(everest_io_tests
  PRIVATE
        GTest::gtest_main
        everest::io
)

include(GoogleTest)
gtest_discover_tests(everest_io_tests TEST_PREFIX "io.")
