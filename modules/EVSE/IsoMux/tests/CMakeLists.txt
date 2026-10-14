get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)

set(CONNECTION_HANDLE_TEST_NAME isomux_connection_handle_test)
add_executable(${CONNECTION_HANDLE_TEST_NAME})
add_dependencies(${CONNECTION_HANDLE_TEST_NAME} generate_cpp_files)

target_include_directories(${CONNECTION_HANDLE_TEST_NAME} PRIVATE
    .. ../connection
    ${GENERATED_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}/generated/modules/${MODULE_NAME}
    ${CMAKE_BINARY_DIR}/generated/include
)

target_sources(${CONNECTION_HANDLE_TEST_NAME} PRIVATE
    connection_handle_test.cpp
    ../connection/connection.cpp
    ../connection/tls_connection.cpp
    ../log.cpp
    ../tools.cpp
    ../v2g_server.cpp
)

target_link_libraries(${CONNECTION_HANDLE_TEST_NAME} PRIVATE
    GTest::gtest_main
    cbv2g::din
    cbv2g::iso2
    cbv2g::tp
    everest::framework
    everest::evse_security
    everest::tls
    everest::everest_api_types
    -lpthread
)

add_test(${CONNECTION_HANDLE_TEST_NAME} ${CONNECTION_HANDLE_TEST_NAME})
ev_register_test_target(${CONNECTION_HANDLE_TEST_NAME})
