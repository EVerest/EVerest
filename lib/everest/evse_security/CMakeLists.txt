cmake_minimum_required(VERSION 3.14)

project(everest-evse_security VERSION 0.10.1
        DESCRIPTION "Implementation of EVSE related security operations"
		LANGUAGES CXX C
)

# options
option(${PROJECT_NAME}_BUILD_TESTING "Build unit tests, used if included as dependency" OFF)
option(BUILD_TESTING "Build unit tests, used if standalone project" OFF)
option(USING_TPM2 "Include code for using OpenSSL 3 and the tpm2 provider" OFF)
option(USING_CUSTOM_PROVIDER "Include code for using OpenSSL 3 and the custom provider" OFF)

if((${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME} OR ${PROJECT_NAME}_BUILD_TESTING) AND BUILD_TESTING)
    set(LIBEVSE_SECURITY_BUILD_TESTING ON)
endif()

if(LIBEVSE_SECURITY_BUILD_TESTING AND EVEREST_ENABLE_COVERAGE)
    evc_include(CodeCoverage)
    append_coverage_compiler_flags()
endif()

if(USING_TPM2 AND USING_CUSTOM_PROVIDER)
    message(FATAL_ERROR, "TPM2 provider and custom provider are incompatible")
endif()

if(USING_TPM2)
    set(CUSTOM_PROVIDER_NAME "tpm2")
endif()

if(USING_CUSTOM_PROVIDER)
    set(CUSTOM_PROVIDER_NAME "custom_provider")
endif()

if(USING_TPM2 OR USING_CUSTOM_PROVIDER)
# OpenSSL property string when using the default provider
    set(PROPQUERY_PROVIDER_DEFAULT "?provider=default")
    # OpenSSL property string when using the tpm2/custom provider
    set(PROPQUERY_PROVIDER_CUSTOM "?provider=${CUSTOM_PROVIDER_NAME}")
endif()

# dependencies
if (DISABLE_EDM)
    find_package(date REQUIRED)
endif()

option(LIBEVSE_SECURITY_USE_BOOST_FILESYSTEM "Usage of boost/filesystem.hpp instead of std::filesystem" OFF)

option(LIBEVSE_CRYPTO_SUPPLIER_OPENSSL "Default OpenSSL cryptography supplier" ON)

# dependencies
if (LIBEVSE_CRYPTO_SUPPLIER_OPENSSL)
    find_package(OpenSSL 3 REQUIRED)
endif()

add_subdirectory(lib)

# packaging
install(
    TARGETS evse_security
    EXPORT everest-core-targets
    LIBRARY
)

install(
    DIRECTORY include/
    TYPE INCLUDE
    PATTERN "detail" EXCLUDE
)

install(
    DIRECTORY 3rd_party/
    TYPE INCLUDE
)

if(LIBEVSE_SECURITY_BUILD_TESTING)
    include(CTest)
    add_subdirectory(tests)
    set(CMAKE_BUILD_TYPE Debug)
endif()
