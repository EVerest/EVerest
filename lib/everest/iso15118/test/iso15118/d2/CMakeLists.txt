include(Catch)

add_executable(test_d2_crypto crypto.cpp)

target_link_libraries(test_d2_crypto
    PRIVATE
        iso15118
        OpenSSL::Crypto
        Catch2::Catch2WithMain
)

catch_discover_tests(test_d2_crypto)
