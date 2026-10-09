add_library(tls STATIC)
add_library(everest::tls ALIAS tls)
ev_register_library_target(tls)

find_package(OpenSSL 3)

target_sources(tls
    PRIVATE
        extensions/status_request.cpp
        extensions/trusted_ca_keys.cpp
        src/openssl_conv.cpp
        src/openssl_util.cpp
        src/tls.cpp
)

target_compile_definitions(tls PRIVATE
    -DLIBEVSE_CRYPTO_SUPPLIER_OPENSSL
)

target_include_directories(tls
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# FIXME (aw): check whether all of this needs to be publicly exposed
target_link_libraries(tls
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
        everest::evse_security
        everest::util
)

set_target_properties(tls
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(
    TARGETS
        tls
    EXPORT everest-base-targets
    LIBRARY
)

install(
    DIRECTORY include/
    TYPE INCLUDE
)

# tls.hpp includes them relative to its own directory
install(
    DIRECTORY extensions/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/everest/tls/extensions
    FILES_MATCHING PATTERN "*.hpp"
)
