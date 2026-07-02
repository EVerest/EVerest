add_executable(everest_io_tests
  endpoint_test.cpp
  fd_event_client_alias_test.cpp
  socket_test.cpp
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

if(NOT EVEREST_IO_ENABLE_TLS)
    return()
endif()

find_package(OpenSSL 3 REQUIRED)

# The alias regression test asserts the tls_client alias too; that header pulls
# in OpenSSL transitively. Make everest_io_tests aware of TLS and able to find
# the OpenSSL headers so the guarded static_assert compiles when TLS is on.
target_compile_definitions(everest_io_tests PRIVATE EVEREST_IO_ENABLE_TLS)
target_link_libraries(everest_io_tests
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
)

add_executable(everest_io_tls_test
    tls_test_main.cpp
    tls_server_socket_test.cpp
    tls_listener_test.cpp
    tls_client_socket_test.cpp
    tls_e2e_test.cpp
)

target_link_libraries(everest_io_tls_test
    PRIVATE
        GTest::gtest
        everest::io
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Reuse existing libtls test fixtures (server_chain.pem etc) by adding the
# libtls test dir to the test binary's working data path. Existing libtls
# pki cookbook lives at lib/everest/tls/tests/pki/. The custom main in
# tls_test_main.cpp invokes ./pki.sh at startup so this binary does not
# depend on libtls's tls_test having run first under ctest.
add_dependencies(everest_io_tls_test tls_test_files_target)

add_test(
    NAME everest_io_tls_test
    COMMAND everest_io_tls_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/lib/everest/tls/tests
)

# Both this binary and libtls's tls_test regenerate the PKI fixture via
# ./pki.sh in the same working directory; serialize them under ctest so one
# does not regenerate certs mid-handshake of the other.
set_tests_properties(everest_io_tls_test PROPERTIES RESOURCE_LOCK tls_pki)
