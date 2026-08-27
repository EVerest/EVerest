cmake_minimum_required(VERSION 3.14)

project(everest-opcp VERSION 0.1.0
        DESCRIPTION "Client for the Open Plug&Charge Protocol (OPCP): OAuth2, EST enrollment, root certificate pool"
        LANGUAGES CXX
)

find_package(everest-cmake 0.1 REQUIRED
    PATHS ../everest-cmake
)

option(${PROJECT_NAME}_BUILD_TESTING "Build unit tests, used if included as dependency" OFF)
option(BUILD_TESTING "Build unit tests, used if standalone project" OFF)
option(OPCP_BUILD_TOOLS "Build the opcp-enroll command line tool" ON)

if((${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME} OR ${PROJECT_NAME}_BUILD_TESTING) AND BUILD_TESTING)
    set(OPCP_BUILD_TESTING ON)
endif()

# libcurl is an optional everest-core dependency (EVEREST_DEPENDENCY_ENABLED_LIBCURL, enabled by the modules
# listed in module-dependencies.cmake). Without it this library cannot be built, so skip it silently.
if(NOT TARGET CURL::libcurl)
    find_package(CURL 7.84.0 QUIET)
endif()
if(NOT TARGET CURL::libcurl)
    message(STATUS "libcurl not available - skipping everest::opcp (OPCP certificate enrollment)")
    return()
endif()

find_package(OpenSSL 3 REQUIRED)
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json REQUIRED)
endif()

add_library(opcp)
add_library(everest::opcp ALIAS opcp)

target_sources(opcp
    PRIVATE
        lib/types.cpp
        lib/environment.cpp
        lib/curl_http_client.cpp
        lib/oauth2.cpp
        lib/identifiers.cpp
        lib/pkcs.cpp
        lib/est_client.cpp
        lib/rcp_client.cpp
        lib/vra_client.cpp
        lib/enroller.cpp
)

target_include_directories(opcp
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(opcp
    PUBLIC
        everest::evse_security
        nlohmann_json::nlohmann_json
    PRIVATE
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
)

set_target_properties(opcp PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(opcp PUBLIC cxx_std_17)

if(OPCP_BUILD_TOOLS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/opcp-enroll/CMakeLists.txt)
    add_subdirectory(tools/opcp-enroll)
endif()

if(OPCP_BUILD_TESTING)
    include(CTest)
    add_subdirectory(tests)
endif()
