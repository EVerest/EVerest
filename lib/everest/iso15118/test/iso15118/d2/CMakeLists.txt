include(Catch)

add_executable(test_d2_crypto crypto.cpp)

target_link_libraries(test_d2_crypto
    PRIVATE
        iso15118
        OpenSSL::Crypto
        Catch2::Catch2WithMain
)

catch_discover_tests(test_d2_crypto)

add_executable(test_d2_crypto_roundtrip crypto_roundtrip.cpp)
target_link_libraries(test_d2_crypto_roundtrip PRIVATE iso15118 OpenSSL::Crypto everest::tls Catch2::Catch2WithMain)
if(ISO15118_BUILD_EV_SIDE)
    target_sources(test_d2_crypto_roundtrip PRIVATE ../ev/d2/helper.cpp)
    target_compile_definitions(test_d2_crypto_roundtrip PRIVATE ISO15118_TEST_EV_SIDE)
endif()
catch_discover_tests(test_d2_crypto_roundtrip)
