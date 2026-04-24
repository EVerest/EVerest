find_package(OpenSSL 3)

set(TLS_TEST_FILES
        alt_openssl-pki.conf
        iso_pkey.asn1
        openssl-pki.conf
        ocsp_response.der
        pki.sh
        pki-multi-curve.sh
        pki-tpm.sh
)

add_custom_command(
    OUTPUT ${TLS_TEST_FILES}
    COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/pki
    COMMAND cd pki && cp ${TLS_TEST_FILES} ${CMAKE_CURRENT_BINARY_DIR}/
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(tls_test_files_target
    DEPENDS ${TLS_TEST_FILES}
)

set(TLS_GTEST_NAME tls_test)
add_executable(${TLS_GTEST_NAME})
add_dependencies(${TLS_GTEST_NAME} tls_test_files_target)

# Multi-curve PKI generation for signature-algorithm pinning tests.
# Builds a separate server + client chain per NIST curve so the test suite
# can exercise P-256, P-384 and P-521 independently.
foreach(CURVE P-256 P-384 P-521)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/server_cert.pem
               ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/server_priv.pem
               ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/server_chain.pem
               ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/server_root_cert.pem
               ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/server_ca_cert.pem
               ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/client_cert.pem
               ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/client_priv.pem
               ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/client_chain.pem
               ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/client_root_cert.pem
               ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/client_ca_cert.pem
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/pki/pki-multi-curve.sh ${CURVE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/pki/pki-multi-curve.sh
                ${CMAKE_CURRENT_SOURCE_DIR}/pki/openssl-pki.conf
                ${CMAKE_CURRENT_BINARY_DIR}/openssl-pki.conf
    )
    list(APPEND TLS_MULTI_CURVE_PKI_FILES
        ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/server_cert.pem
        ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/server_chain.pem
        ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/server_priv.pem
        ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/server_root_cert.pem
        ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/client_chain.pem
        ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/client_priv.pem
        ${CMAKE_CURRENT_BINARY_DIR}/pki-${CURVE}/client_root_cert.pem
    )
endforeach()

add_custom_target(tls_multi_curve_pki_target DEPENDS ${TLS_MULTI_CURVE_PKI_FILES})
add_dependencies(${TLS_GTEST_NAME} tls_multi_curve_pki_target)

target_include_directories(${TLS_GTEST_NAME} PRIVATE
    ..
    ../include
)

target_compile_definitions(${TLS_GTEST_NAME} PRIVATE
    -DUNIT_TEST
    -DLIBEVSE_CRYPTO_SUPPLIER_OPENSSL
)

target_sources(${TLS_GTEST_NAME} PRIVATE
    gtest_main.cpp
    crypto_test.cpp
    openssl_util_test.cpp
    tls_test.cpp
    tls_connection_test.cpp
    ../extensions/helpers.cpp
    ../extensions/status_request.cpp
    ../extensions/trusted_ca_keys.cpp
    ../src/openssl_conv.cpp
    ../src/openssl_util.cpp
    ../src/tls.cpp
)

if(USING_TPM2)
    target_sources(${TLS_GTEST_NAME} PRIVATE
        tls_connection_test_tpm.cpp
    )
    target_compile_definitions(${TLS_GTEST_NAME} PRIVATE
        USING_TPM2
    )
endif()

target_link_libraries(${TLS_GTEST_NAME}
    PRIVATE
        GTest::gtest
        OpenSSL::SSL
        OpenSSL::Crypto
        everest::evse_security
        everest::util
)

set(TLS_MAIN_NAME tls_server)
add_executable(${TLS_MAIN_NAME})
add_dependencies(${TLS_MAIN_NAME} tls_test_files_target)

target_include_directories(${TLS_MAIN_NAME} PRIVATE
    ..
    ../include
)

target_compile_definitions(${TLS_MAIN_NAME} PRIVATE
    -DUNIT_TEST
)

target_sources(${TLS_MAIN_NAME} PRIVATE
    tls_main.cpp
    ../extensions/helpers.cpp
    ../extensions/status_request.cpp
    ../extensions/trusted_ca_keys.cpp
    ../src/openssl_util.cpp
    ../src/tls.cpp
)

target_link_libraries(${TLS_MAIN_NAME}
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        everest::evse_security
        everest::util
)

set(TLS_CLIENT_NAME tls_client)
add_executable(${TLS_CLIENT_NAME})
add_dependencies(${TLS_CLIENT_NAME} tls_test_files_target)

target_include_directories(${TLS_CLIENT_NAME} PRIVATE
    ..
    ../include
)

target_compile_definitions(${TLS_CLIENT_NAME} PRIVATE
    -DUNIT_TEST
)

target_sources(${TLS_CLIENT_NAME} PRIVATE
    tls_client_main.cpp
    ../extensions/helpers.cpp
    ../extensions/status_request.cpp
    ../extensions/trusted_ca_keys.cpp
    ../src/openssl_util.cpp
    ../src/tls.cpp
)

target_link_libraries(${TLS_CLIENT_NAME}
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        everest::evse_security
        everest::util
)

set(TLS_PATCH_NAME patched_test)
add_executable(${TLS_PATCH_NAME})
add_dependencies(${TLS_PATCH_NAME} tls_test_files_target)

target_include_directories(${TLS_PATCH_NAME} PRIVATE
    ..
    ../include
)

target_compile_definitions(${TLS_PATCH_NAME} PRIVATE
    -DUNIT_TEST
)

target_sources(${TLS_PATCH_NAME} PRIVATE
    patched_test.cpp
    ../extensions/helpers.cpp
    ../extensions/status_request.cpp
    ../extensions/trusted_ca_keys.cpp
    ../src/openssl_util.cpp
    ../src/tls.cpp
)

target_link_libraries(${TLS_PATCH_NAME}
    PRIVATE
        GTest::gtest_main
        OpenSSL::SSL
        OpenSSL::Crypto
        everest::evse_security
        everest::util
)

add_test(${TLS_GTEST_NAME} ${TLS_GTEST_NAME})
ev_register_test_target(${TLS_GTEST_NAME})
