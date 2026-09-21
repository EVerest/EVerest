include(Catch)

add_executable(test_timeouts timeouts.cpp)

target_link_libraries(test_timeouts
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_timeouts)

add_executable(test_sae_default_der_control sae_default_der_control.cpp)

target_link_libraries(test_sae_default_der_control
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_sae_default_der_control)

add_executable(test_sae_der_control_convert sae_der_control_convert.cpp)

target_link_libraries(test_sae_der_control_convert
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_sae_der_control_convert)

add_executable(test_sae_der_control_cl_res sae_der_control_cl_res.cpp)

target_link_libraries(test_sae_der_control_cl_res
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_sae_der_control_cl_res)

add_executable(test_sae_modes sae_modes.cpp)

target_link_libraries(test_sae_modes
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_sae_modes)

add_executable(test_d20_crypto crypto.cpp)

target_link_libraries(test_d20_crypto
    PRIVATE
        iso15118
        OpenSSL::Crypto
        Catch2::Catch2WithMain
)

catch_discover_tests(test_d20_crypto)
