cmake_minimum_required(VERSION 3.14)

project(everest-sqlite VERSION 0.1.5
        DESCRIPTION "SQLite wrapper for EVerest"
        LANGUAGES CXX C
)

option(${PROJECT_NAME}_BUILD_TESTING "Build unit tests, used if included as dependency" OFF)
option(BUILD_TESTING "Build unit tests, used if standalone project" OFF)

if((${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME} OR ${PROJECT_NAME}_BUILD_TESTING) AND BUILD_TESTING)
    set(EVEREST_SQLITE_BUILD_TESTING ON)
    # this policy allows us to link gcov to targets defined in other directories
    if(POLICY CMP0079)
        set(CMAKE_POLICY_DEFAULT_CMP0079 NEW)
    endif()
endif()

find_package(SQLite3 REQUIRED)
if(NOT TARGET SQLite3::SQLite3) # CMake < 4.3
  set_target_properties(SQLite::SQLite3 PROPERTIES IMPORTED_GLOBAL TRUE)
  add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()

if (DISABLE_EDM AND EVEREST_SQLITE_BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/release-1.12.1.zip
    )
    FetchContent_MakeAvailable(googletest)
endif()


# options
option(${PROJECT_NAME}_BUILD_TESTING "Build unit tests, used if included as dependency" OFF)
option(BUILD_TESTING "Build unit tests, used if standalone project" OFF)
option(EVEREST_SQLITE_USE_BOOST_FILESYSTEM "Usage of boost/filesystem.hpp instead of std::filesystem" OFF)

add_subdirectory(lib)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# packaging
set_target_properties(everest_sqlite PROPERTIES EXPORT_NAME sqlite)

install(
    TARGETS everest_sqlite
    EXPORT ${EVEREST_EXPORT_SET}
    LIBRARY
)

install(
    DIRECTORY include/
    TYPE INCLUDE
)

install(
    FILES cmake/CollectMigrationFiles.cmake
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}/cmake
)

if(EVEREST_SQLITE_BUILD_TESTING)
    include(CTest)
    add_subdirectory(tests)
    set(CMAKE_BUILD_TYPE Debug)
endif()
