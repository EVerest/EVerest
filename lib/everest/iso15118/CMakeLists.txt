cmake_minimum_required(VERSION 3.14)

project(iso15118
    VERSION 0.9.1
    DESCRIPTION "iso15118 library suite"
	LANGUAGES CXX C
)

find_package(OpenSSL 3 REQUIRED)

# options
option(${PROJECT_NAME}_BUILD_TESTING "Build unit tests, used if included as dependency" OFF)
option(BUILD_TESTING "Build unit tests, used if standalone project" OFF)

# list of compile options
set(ISO15118_COMPILE_OPTIONS_WARNING "-Wall;-Wextra;-Wno-unused-function;-Werror" CACHE STRING "A list of compile options used")
message(STATUS "Building libiso15118 with the following compile options: ${ISO15118_COMPILE_OPTIONS_WARNING}")

if((${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME} OR ${PROJECT_NAME}_BUILD_TESTING) AND BUILD_TESTING)
    set(ISO15118_BUILD_TESTING ON)
endif()

add_subdirectory(src)

if (ISO15118_BUILD_TESTING)
    include(CTest)
    add_subdirectory(test)
endif()

install(
    TARGETS
        iso15118
    EXPORT everest-core-targets
)

install(
    DIRECTORY include/
    TYPE INCLUDE
    PATTERN "detail" EXCLUDE
)
