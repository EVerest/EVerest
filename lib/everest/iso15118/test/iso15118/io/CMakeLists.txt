include(Catch)

add_executable(test_logging logging.cpp)

target_link_libraries(test_logging
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_logging)

add_executable(test_sdp_server sdp_server_test.cpp)
target_link_libraries(test_sdp_server PRIVATE iso15118 Catch2::Catch2WithMain)
catch_discover_tests(test_sdp_server)

add_executable(test_sdp_client sdp_client_test.cpp)
target_link_libraries(test_sdp_client PRIVATE iso15118 Catch2::Catch2WithMain)
catch_discover_tests(test_sdp_client)

add_executable(test_connection_client_plain connection_client_plain_test.cpp)
target_link_libraries(test_connection_client_plain PRIVATE iso15118 Catch2::Catch2WithMain)
# Binds the hardcoded TCP port 50000; share the lock with the other port-50000 tests.
catch_discover_tests(test_connection_client_plain PROPERTIES RESOURCE_LOCK loopback_port_50000)

add_executable(test_connection_client_ssl connection_client_ssl_test.cpp)
add_custom_command(
    TARGET test_connection_client_ssl POST_BUILD
    COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/pki
    COMMAND cd pki && cp pki.sh ${CMAKE_CURRENT_BINARY_DIR}/pki && cp -r configs ${CMAKE_CURRENT_BINARY_DIR}/pki
    COMMAND cd ${CMAKE_CURRENT_BINARY_DIR}/pki && sh pki.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(test_connection_client_ssl PRIVATE PKI_PATH="${CMAKE_CURRENT_BINARY_DIR}/pki")
target_link_libraries(test_connection_client_ssl PRIVATE iso15118 Catch2::Catch2WithMain)
# Binds the hardcoded TLS port 50000; share the lock with the other port-50000 tests.
catch_discover_tests(test_connection_client_ssl PROPERTIES RESOURCE_LOCK loopback_port_50000)

add_executable(test_connection_client_ssl_tls12 connection_client_ssl_tls12_test.cpp)
add_custom_command(
    TARGET test_connection_client_ssl_tls12 POST_BUILD
    COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/pki
    COMMAND cd pki && cp pki.sh ${CMAKE_CURRENT_BINARY_DIR}/pki && cp -r configs ${CMAKE_CURRENT_BINARY_DIR}/pki
    COMMAND cd ${CMAKE_CURRENT_BINARY_DIR}/pki && sh pki.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(test_connection_client_ssl_tls12 PRIVATE PKI_PATH="${CMAKE_CURRENT_BINARY_DIR}/pki")
target_link_libraries(test_connection_client_ssl_tls12 PRIVATE iso15118 Catch2::Catch2WithMain)
catch_discover_tests(test_connection_client_ssl_tls12)

add_executable(connection_openssl_test)
add_custom_command(
    TARGET connection_openssl_test POST_BUILD
    COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/pki
    COMMAND cd pki && cp pki.sh ${CMAKE_CURRENT_BINARY_DIR}/pki && cp -r configs ${CMAKE_CURRENT_BINARY_DIR}/pki
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
target_sources(connection_openssl_test
    PRIVATE
        connection_openssl.cpp
)
target_link_libraries(connection_openssl_test
    PRIVATE
        iso15118::iso15118
)
