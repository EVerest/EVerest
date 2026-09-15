project(iso15118
    VERSION 0.9.1
    DESCRIPTION "iso15118 library suite"
	LANGUAGES CXX C
)

find_package(OpenSSL 3 REQUIRED)

# options
option(ISO15118_BUILD_EV_SIDE "Build the EV (EVCC) side of the stack, not just the SECC" OFF)

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
# ev/ and the response variants declare symbols that are only in the library when the EV
# side was built, so a charger-only install must not ship those headers.
set(ISO15118_HEADER_EXCLUDES)
if(NOT ISO15118_BUILD_EV_SIDE)
    list(APPEND ISO15118_HEADER_EXCLUDES PATTERN "ev" EXCLUDE PATTERN "response_variant.hpp" EXCLUDE)
endif()

install(
    DIRECTORY include/
    TYPE INCLUDE
    ${ISO15118_HEADER_EXCLUDES}
)
