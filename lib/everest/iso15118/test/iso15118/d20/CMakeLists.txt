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
