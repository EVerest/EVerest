include(Catch)

function(create_ev_transport_test_target NAME)
    add_executable(test_ev_${NAME} ${NAME}.cpp)
    target_link_libraries(test_ev_${NAME}
        PRIVATE
            iso15118
            Catch2::Catch2WithMain
    )
    catch_discover_tests(test_ev_${NAME})
endfunction()

create_ev_transport_test_target(sdp_client)
create_ev_transport_test_target(data_client)

# The TLS data path needs a V2G PKI; reuse the cookbook next to the SECC-side tests and generate it
# into this target's own binary dir, so a parallel build never has two pki.sh runs in one directory.
set(EV_TRANSPORT_PKI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../io/pki)

add_executable(test_ev_data_client_tls data_client_tls.cpp)
add_custom_command(
    TARGET test_ev_data_client_tls POST_BUILD
    COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/pki/configs
    COMMAND cp ${EV_TRANSPORT_PKI_DIR}/pki.sh ${CMAKE_CURRENT_BINARY_DIR}/pki
    COMMAND cp ${EV_TRANSPORT_PKI_DIR}/configs/. ${CMAKE_CURRENT_BINARY_DIR}/pki/configs -r
    # Generating the PKI costs seconds of openssl; only do it once per build dir.
    COMMAND cd ${CMAKE_CURRENT_BINARY_DIR}/pki && test -d certs/ca/v2g || sh pki.sh
)
target_compile_definitions(test_ev_data_client_tls PRIVATE PKI_PATH="${CMAKE_CURRENT_BINARY_DIR}/pki")
target_link_libraries(test_ev_data_client_tls
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
        # libtls is a private dependency of iso15118, so the headers the libio TLS listener config
        # pulls in need naming here.
        everest::tls
        OpenSSL::SSL
        OpenSSL::Crypto
)
catch_discover_tests(test_ev_data_client_tls)
