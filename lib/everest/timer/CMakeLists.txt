project(everest-timer
    VERSION 0.1.4
    DESCRIPTION "EVerest timer library"
    LANGUAGES CXX C
)

# options
option(BUILD_EXAMPLES "Build examples" OFF)
option(CMAKE_RUN_CLANG_TIDY "Run clang-tidy" OFF)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# dependencies
find_package(Boost)
if(Boost_VERSION_STRING VERSION_LESS "1.69.0")
    find_package(Boost
        COMPONENTS
            system
        REQUIRED
    )
endif()

if (DISABLE_EDM)
    find_package(date REQUIRED)
endif()


# logging library
add_subdirectory(lib)

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# packaging
install(
    TARGETS timer
    EXPORT everest-base-targets
)

install(
    DIRECTORY include/
    TYPE INCLUDE
)
