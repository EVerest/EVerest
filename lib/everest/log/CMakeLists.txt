project(everest-log
    VERSION 0.3.0
    DESCRIPTION "EVerest logging library"
    LANGUAGES CXX C
)

# options
option(BUILD_BACKTRACE_SUPPORT "Build with backtrace support from libbacktrace" OFF)
option(BUILD_EXAMPLES "Build liblog example binaries." OFF)
option(CMAKE_RUN_CLANG_TIDY "Run clang-tidy" OFF)
option(LIBLOG_USE_BOOST_FILESYSTEM "Usage of boost/filesystem.hpp instead of std::filesystem" OFF)

if (DISABLE_EDM AND BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/release-1.12.1.zip
    )
    FetchContent_MakeAvailable(googletest)
endif()

# library dependencies

if (LIBLOG_USE_BOOST_FILESYSTEM) 
    message(STATUS "Using boost/filesystem instead of std::filesystem")
    find_package(Boost COMPONENTS log_setup log filesystem REQUIRED)
else()
    find_package(Boost COMPONENTS log_setup log REQUIRED)
endif()


# third party dependencies
add_subdirectory(3rd_party)

# logging library
add_subdirectory(lib)

# packaging
install(
    FILES examples/logging.ini
    DESTINATION ${CMAKE_INSTALL_DATADIR}/everest/log
    RENAME example-config.ini
)

set_target_properties(everest_log PROPERTIES EXPORT_NAME log)

install(
    TARGETS everest_log
    EXPORT everest-base-targets
)

install(
    DIRECTORY include/
    TYPE INCLUDE
)

if (BUILD_BACKTRACE_SUPPORT)
#   FIXME (aw): if statically build, we would need to install libbacktrace too
endif()


# testing
if(BUILD_TESTING)
    add_subdirectory(tests)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if(BUILD_EXAMPLES)
    message("Building liblog example binaries.")
    add_subdirectory(examples)
else()
    message("Not building liblog example binaries.")
endif()

# configure clang-tidy if requested
if(CMAKE_RUN_CLANG_TIDY)
    message("Running clang-tidy")
    string(CONCAT CLANG_TIDY_CHECKS "*,"
                                    "-llvmlibc*,"
                                    "-fuchsia-default-arguments-calls,"
                                    "-fuchsia-overloaded-operator,"
                                    "-fuchsia-statically-constructed-objects,"
                                    "-readability-function-cognitive-complexity,"
                                    "-modernize-use-trailing-return-type,"
                                    "-abseil-string-find-startswith,"
                                    "-abseil-string-find-str-contains,"
                                    ";")
    set(CMAKE_CXX_CLANG_TIDY 
    clang-tidy;
    -header-filter='.*'
    -checks=${CLANG_TIDY_CHECKS})
endif()

# build doxygen documentation if doxygen is available
find_package(Doxygen)
if(DOXYGEN_FOUND)
    set( DOXYGEN_OUTPUT_DIRECTORY dist/docs )
    doxygen_add_docs(doxygen-${PROJECT_NAME} include lib src)
else()
    message("Doxygen is needed to generate documentation")
endif()
