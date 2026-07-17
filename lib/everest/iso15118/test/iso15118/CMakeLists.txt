add_subdirectory(d20)
add_subdirectory(fsm)
add_subdirectory(io)
add_subdirectory(session)
add_subdirectory(states)
add_subdirectory(states_ev)

include(Catch)
find_package(Threads REQUIRED)

# EVCC <-> SECC in-process loopback integration tests. All bind the same TCP port 50000
# (ConnectionPlain / ConnectionSSL hardcode it), so they share a RESOURCE_LOCK to never run
# concurrently under `ctest -j`.

# DC over plain TCP
add_executable(test_evcc_secc_dc_loopback evcc_secc_dc_loopback.cpp)
target_link_libraries(test_evcc_secc_dc_loopback
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
        Threads::Threads
)
catch_discover_tests(test_evcc_secc_dc_loopback PROPERTIES TIMEOUT 120 RESOURCE_LOCK loopback_port_50000)

# DIN SPEC 70121 DC over plain TCP
add_executable(test_evcc_secc_din_dc_loopback evcc_secc_din_dc_loopback.cpp)
target_link_libraries(test_evcc_secc_din_dc_loopback
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
        Threads::Threads
)
catch_discover_tests(test_evcc_secc_din_dc_loopback PROPERTIES TIMEOUT 120 RESOURCE_LOCK loopback_port_50000)

# ISO 15118-2 DC over plain TCP
add_executable(test_evcc_secc_iso2_dc_loopback evcc_secc_iso2_dc_loopback.cpp)
target_link_libraries(test_evcc_secc_iso2_dc_loopback
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
        Threads::Threads
)
catch_discover_tests(test_evcc_secc_iso2_dc_loopback PROPERTIES TIMEOUT 120 RESOURCE_LOCK loopback_port_50000)

# ISO 15118-2 AC over plain TCP
add_executable(test_evcc_secc_iso2_ac_loopback evcc_secc_iso2_ac_loopback.cpp)
target_link_libraries(test_evcc_secc_iso2_ac_loopback
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
        Threads::Threads
)
catch_discover_tests(test_evcc_secc_iso2_ac_loopback PROPERTIES TIMEOUT 120 RESOURCE_LOCK loopback_port_50000)

# AC_BPT over plain TCP
add_executable(test_evcc_secc_ac_loopback evcc_secc_ac_loopback.cpp)
target_link_libraries(test_evcc_secc_ac_loopback
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
        Threads::Threads
)
catch_discover_tests(test_evcc_secc_ac_loopback PROPERTIES TIMEOUT 120 RESOURCE_LOCK loopback_port_50000)

# DC over mutual TLS 1.3 (reuses the io test PKI generation approach)
add_executable(test_evcc_secc_dc_loopback_tls evcc_secc_dc_loopback_tls.cpp)
add_custom_command(
    TARGET test_evcc_secc_dc_loopback_tls POST_BUILD
    COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/pki
    COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR}/io/pki && cp pki.sh ${CMAKE_CURRENT_BINARY_DIR}/pki && cp -r configs ${CMAKE_CURRENT_BINARY_DIR}/pki
    COMMAND cd ${CMAKE_CURRENT_BINARY_DIR}/pki && sh pki.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(test_evcc_secc_dc_loopback_tls PRIVATE PKI_PATH="${CMAKE_CURRENT_BINARY_DIR}/pki")
target_link_libraries(test_evcc_secc_dc_loopback_tls
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
        Threads::Threads
)
catch_discover_tests(test_evcc_secc_dc_loopback_tls PROPERTIES TIMEOUT 120 RESOURCE_LOCK loopback_port_50000)

# add_executable(secc)
#
# target_sources(secc
#     PRIVATE
#         secc.cpp
# )
#
# target_link_libraries(secc
#     PRIVATE
#         iso15118::iso15118
# )

# ISO 15118-2 Plug-and-Charge crypto round-trip (EVCC signing + contract-key decryption)
add_executable(test_pnc_crypto crypto/pnc_crypto.cpp)
target_link_libraries(test_pnc_crypto
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
        OpenSSL::Crypto
)
catch_discover_tests(test_pnc_crypto)
