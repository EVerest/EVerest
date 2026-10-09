project(everest-sqlite VERSION 0.1.5
        DESCRIPTION "SQLite wrapper for EVerest"
        LANGUAGES CXX C
)


find_package(SQLite3 REQUIRED)

if (DISABLE_EDM AND BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/release-1.12.1.zip
    )
    FetchContent_MakeAvailable(googletest)
endif()


# options
option(EVEREST_SQLITE_USE_BOOST_FILESYSTEM "Usage of boost/filesystem.hpp instead of std::filesystem" OFF)

add_subdirectory(lib)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# packaging
set_target_properties(everest_sqlite PROPERTIES EXPORT_NAME sqlite)

install(
    TARGETS everest_sqlite
    EXPORT everest-base-targets
    LIBRARY
)

install(
    DIRECTORY include/
    TYPE INCLUDE
)

install(
    FILES cmake/CollectMigrationFiles.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/everest-base
)

if(BUILD_TESTING)
    add_subdirectory(tests)
    set(CMAKE_BUILD_TYPE Debug)
endif()
