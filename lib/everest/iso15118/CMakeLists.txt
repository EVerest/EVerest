project(iso15118
    VERSION 0.9.1
    DESCRIPTION "iso15118 library suite"
	LANGUAGES CXX C
)

find_package(OpenSSL 3 REQUIRED)

# options

# list of compile options
set(ISO15118_COMPILE_OPTIONS_WARNING "-Wall;-Wextra;-Wno-unused-function;-Werror" CACHE STRING "A list of compile options used")
message(STATUS "Building libiso15118 with the following compile options: ${ISO15118_COMPILE_OPTIONS_WARNING}")

add_subdirectory(src)

if (BUILD_TESTING)
    add_subdirectory(test)
endif()

install(
    TARGETS
        iso15118
    EXPORT everest-iso15118-targets
)

# detail headers included too: modules/EVSE/Evse15118D20 uses iso15118/detail/d20/config_validation.hpp
install(
    DIRECTORY include/
    TYPE INCLUDE
)
