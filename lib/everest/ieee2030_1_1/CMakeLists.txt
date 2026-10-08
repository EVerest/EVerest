cmake_minimum_required(VERSION 3.14)

project(iee2030
    VERSION 0.1
    DESCRIPTION "Simple IEEE2030.1.1 library"
	  LANGUAGES CXX
)

# options
option(${PROJECT_NAME}_BUILD_TESTING "Build unit tests, used if included as dependency" OFF)
option(BUILD_TESTING "Build unit tests, used if standalone project" OFF)

# list of compile options
set(IEEE2030_COMPILE_OPTIONS_WARNING "-Wall;-Wextra;-Wno-unused-function;-Werror" CACHE STRING "A list of compile options used")
message(STATUS "Building libieee2030_1_1 with the following compile options: ${IEEE2030_COMPILE_OPTIONS_WARNING}")

if((${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME} OR ${PROJECT_NAME}_BUILD_TESTING) AND BUILD_TESTING)
    set(IEEE2030_BUILD_TESTING ON)
endif()

add_subdirectory(src)

if (IEEE2030_BUILD_TESTING)
    include(CTest)
    add_subdirectory(test)
endif()

install(
    TARGETS
        ieee2030
    EXPORT everest-core-targets
)

install(
    DIRECTORY include/
    TYPE INCLUDE
    PATTERN "detail" EXCLUDE
)
