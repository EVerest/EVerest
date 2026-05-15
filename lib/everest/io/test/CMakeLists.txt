if(NOT EVEREST_IO_ENABLE_TLS)
    return()
endif()

find_package(OpenSSL 3)

add_executable(everest_io_tls_test
    tls_server_socket_test.cpp
    tls_listener_test.cpp
    tls_client_socket_test.cpp
)

target_link_libraries(everest_io_tls_test
    PRIVATE
        GTest::gtest
        GTest::gtest_main
        everest::io
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Reuse existing libtls test fixtures (server_chain.pem etc) by adding the
# libtls test dir to the test binary's working data path. Existing libtls
# pki cookbook lives at lib/everest/tls/tests/pki/.
add_dependencies(everest_io_tls_test tls_test_files_target)

add_test(
    NAME everest_io_tls_test
    COMMAND everest_io_tls_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/lib/everest/tls/tests
)
