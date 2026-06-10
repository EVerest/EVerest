include(Catch)

add_executable(test_logging logging.cpp)

target_link_libraries(test_logging
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_logging)

add_executable(socket_helper_test socket_helper_test.cpp)
target_link_libraries(socket_helper_test
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)
catch_discover_tests(socket_helper_test)

add_executable(test_sdp_server sdp_server_test.cpp)
target_link_libraries(test_sdp_server PRIVATE iso15118 Catch2::Catch2WithMain)
catch_discover_tests(test_sdp_server)

add_executable(test_socket_keepalive socket_keepalive.cpp)
target_link_libraries(test_socket_keepalive PRIVATE iso15118 Catch2::Catch2WithMain)
catch_discover_tests(test_socket_keepalive)

add_executable(test_connection_plain connection_plain.cpp)
target_link_libraries(test_connection_plain PRIVATE iso15118 Catch2::Catch2WithMain)
catch_discover_tests(test_connection_plain
    PROPERTIES RESOURCE_LOCK iso15118_tls_port_50000
)

add_executable(connection_openssl_test)
add_custom_command(
    TARGET connection_openssl_test POST_BUILD
    COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/pki
    COMMAND cd pki && cp pki.sh ${CMAKE_CURRENT_BINARY_DIR}/pki && cp -r configs ${CMAKE_CURRENT_BINARY_DIR}/pki
    COMMAND cd ${CMAKE_CURRENT_BINARY_DIR}/pki && test -d certs/ca/v2g || sh pki.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
target_sources(connection_openssl_test
    PRIVATE
        connection_openssl.cpp
)
target_link_libraries(connection_openssl_test
    PRIVATE
        iso15118::iso15118
        Catch2::Catch2WithMain
        OpenSSL::SSL
        OpenSSL::Crypto
)
catch_discover_tests(connection_openssl_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    PROPERTIES RESOURCE_LOCK iso15118_tls_port_50000
)
