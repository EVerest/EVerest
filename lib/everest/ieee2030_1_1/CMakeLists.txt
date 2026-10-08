cmake_minimum_required(VERSION 3.14)

project(iee2030
    VERSION 0.1
    DESCRIPTION "Simple IEEE2030.1.1 library"
	  LANGUAGES CXX
)

# options

# list of compile options
set(IEEE2030_COMPILE_OPTIONS_WARNING "-Wall;-Wextra;-Wno-unused-function;-Werror" CACHE STRING "A list of compile options used")
message(STATUS "Building libieee2030_1_1 with the following compile options: ${IEEE2030_COMPILE_OPTIONS_WARNING}")

add_subdirectory(src)

if (BUILD_TESTING)
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
