cmake_minimum_required(VERSION 3.14)

project(everest-framework
    VERSION 0.25.0
    DESCRIPTION "The open operating system for e-mobility charging stations"
    LANGUAGES CXX C
)

if(DEFINED EVEREST_IO_WITH_MQTT AND NOT EVEREST_IO_WITH_MQTT)
    message(FATAL_ERROR "everest-framework requires MQTT support in everest::io. "
            "Set EVEREST_IO_WITH_MQTT=ON or exclude framework from the build "
            "(e.g. via EVEREST_EXCLUDE_LIBS=framework).")
endif()

find_package(everest-cmake 0.5 REQUIRED
    PATHS ../everest-cmake
)

# options
option(${PROJECT_NAME}_BUILD_TESTING "Build unit tests, used if included as dependency" OFF)
option(BUILD_TESTING "Build unit tests, used if standalone project" OFF)
option(FRAMEWORK_INSTALL "Install the library (shared data might be installed anyway)" ${EVC_MAIN_PROJECT})
option(CMAKE_RUN_CLANG_TIDY "Run clang-tidy" OFF)
option(EVEREST_ENABLE_JS_SUPPORT "Enable everestjs for JavaScript modules" OFF)
option(EVEREST_ENABLE_PY_SUPPORT "Enable everestpy for Python modules" ON)
option(EVEREST_ENABLE_RS_SUPPORT "Enable everestrs for Rust modules" OFF)
option(EVEREST_ENABLE_ADMIN_PANEL_BACKEND "Enable everest admin panel backend" OFF)
option(EVEREST_INSTALL_ADMIN_PANEL "Download and install everest admin panel" OFF)
set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY "latency" CACHE STRING
    "Thread pool scaling policy for framework message handling: latency, greedy, conservative, fixed_size, custom")
set_property(CACHE EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY PROPERTY STRINGS
    latency greedy conservative fixed_size custom)
set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_LATENCY_THRESHOLD_MS "50" CACHE STRING
    "Maximum queued task wait time in milliseconds before latency scaling grows the framework message handler pool")
set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_LATENCY_TICK_MS "5" CACHE STRING
    "Supervisor tick in milliseconds for framework message handler latency scaling")
set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_FIXED_SIZE_THRESHOLD "3" CACHE STRING
    "Queue size threshold for framework message handler fixed-size scaling")
set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_HEADER "" CACHE STRING
    "Header to include when EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY=custom")
set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_TYPE "" CACHE STRING
    "Fully-qualified scaling policy type when EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY=custom")
set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_INCLUDE_DIR "" CACHE PATH
    "Include directory for the custom thread pool scaling policy header")

foreach(_scaling_option IN ITEMS
        EVEREST_FRAMEWORK_THREAD_POOL_SCALING_LATENCY_THRESHOLD_MS
        EVEREST_FRAMEWORK_THREAD_POOL_SCALING_LATENCY_TICK_MS
        EVEREST_FRAMEWORK_THREAD_POOL_SCALING_FIXED_SIZE_THRESHOLD)
    if(NOT ${_scaling_option} MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR
            "${_scaling_option} must be a positive integer, got '${${_scaling_option}}'")
    endif()
endforeach()

if(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY STREQUAL "latency")
    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_ID 1)
    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_HEADER_CONFIG "everest/util/async/thread_pool_scaling.hpp")
    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_TYPE_CONFIG "void")
elseif(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY STREQUAL "greedy")
    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_ID 2)
    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_HEADER_CONFIG "everest/util/async/thread_pool_scaling.hpp")
    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_TYPE_CONFIG "void")
elseif(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY STREQUAL "conservative")
    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_ID 3)
    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_HEADER_CONFIG "everest/util/async/thread_pool_scaling.hpp")
    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_TYPE_CONFIG "void")
elseif(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY STREQUAL "fixed_size")
    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_ID 4)
    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_HEADER_CONFIG "everest/util/async/thread_pool_scaling.hpp")
    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_TYPE_CONFIG "void")
elseif(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY STREQUAL "custom")
    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_ID 5)

    if(NOT EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_HEADER)
        message(FATAL_ERROR
            "EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_HEADER must be set when "
            "EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY=custom")
    endif()
    if(NOT EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_TYPE)
        message(FATAL_ERROR
            "EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_TYPE must be set when "
            "EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY=custom")
    endif()

    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_HEADER_CONFIG
        "${EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_HEADER}")
    set(EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_TYPE_CONFIG
        "${EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY_CUSTOM_TYPE}")
else()
    message(FATAL_ERROR
        "Invalid EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY: "
        "${EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY}. "
        "Allowed values: latency, greedy, conservative, fixed_size, custom")
endif()
ev_setup_cmake_variables_python_wheel()
option(${PROJECT_NAME}_USE_PYTHON_VENV "Use python venv for pip install targets" OFF)
set(${PROJECT_NAME}_PYTHON_VENV_PATH "${CMAKE_BINARY_DIR}/venv" CACHE PATH "Path to python venv")
ev_setup_python_executable(
    USE_PYTHON_VENV ${${PROJECT_NAME}_USE_PYTHON_VENV}
    PYTHON_VENV_PATH ${${PROJECT_NAME}_PYTHON_VENV_PATH}
)

if((${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME} OR ${PROJECT_NAME}_BUILD_TESTING) AND BUILD_TESTING)
    set(EVEREST_FRAMEWORK_BUILD_TESTING ON)
    # this policy allows us to link gcov to targets defined in other directories
    if(POLICY CMP0079)
        set(CMAKE_POLICY_DEFAULT_CMP0079 NEW)
    endif()
    if (NOT CMAKE_BUILD_TYPE)
        if(EVEREST_ENABLE_DEBUG_BUILD)
            set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
        else()
            set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build type" FORCE)
        endif()
    endif()
endif()

# make own cmake modules available
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# this policy allows us to continue using the removed FindBoost module for now
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()

# dependencies
find_package(Boost
    COMPONENTS
        program_options
        thread
    REQUIRED
)

if(Boost_VERSION_STRING VERSION_LESS "1.69.0")
    find_package(Boost
        COMPONENTS
            system
        REQUIRED
    )
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(libcap
    REQUIRED
    IMPORTED_TARGET
    libcap
)

# Make sure stdc++fs is linked for GCC < 9
if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    set(STD_FILESYSTEM_COMPAT_LIB "stdc++fs")
else()
    set(STD_FILESYSTEM_COMPAT_LIB "")
endif()

if(NOT DISABLE_EDM)
    evc_setup_edm()

    # In EDM mode, we can't install exports (because the dependencies usually do not install their exports)
    set(FRAMEWORK_INSTALL OFF)

    if (EVEREST_ENABLE_ADMIN_PANEL_BACKEND)
        # FIXME (aw): libwebsockets/ninja clean doesn't delete recursivly ..
        set_property(
            TARGET websockets_shared
            APPEND
            PROPERTY ADDITIONAL_CLEAN_FILES "${libwebsockets_BINARY_DIR}/include/libwebsockets"
        )

        # FIXME (aw): libwebsockets enum-int-mismatch FIX
        # see https://github.com/warmcat/libwebsockets/pull/2824
        if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_C_COMPILER_VERSION VERSION_GREATER 13.0)
            target_compile_options(websockets_shared PRIVATE -Wno-error=enum-int-mismatch)
        endif()
    endif()
    set_property(TARGET nlohmann_json_schema_validator PROPERTY POSITION_INDEPENDENT_CODE ON)

    # FIXME (aw): add catch2's cmake folder
    if (BUILD_TESTING)
        list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/contrib")
    endif()
else()
    find_package(date REQUIRED)
    find_package(nlohmann_json REQUIRED)
    find_package(nlohmann_json_schema_validator REQUIRED)
    find_package(fmt REQUIRED)
    # ryml is linked by the controller and is part of the link interface of
    # everest::yaml, which the manager executable links against
    find_package(ryml REQUIRED)

    if (EVEREST_ENABLE_ADMIN_PANEL_BACKEND)
        find_package(libwebsockets REQUIRED)
    endif()

    if (BUILD_TESTING)
        find_package(Catch2 REQUIRED)
    endif()

    include(find-mqttc)
endif()

include(${everest-sqlite_SOURCE_DIR}/cmake/CollectMigrationFiles.cmake)

set(EVEREST_FRAMEWORK_GENERATED_INC_DIR ${PROJECT_BINARY_DIR}/generated)
configure_file(
    include/compile_time_settings.hpp.in
    ${EVEREST_FRAMEWORK_GENERATED_INC_DIR}/everest/compile_time_settings.hpp
)

# library code
add_subdirectory(lib)

# executable code
add_subdirectory(src)

# auxillary files
add_subdirectory(schemas)

# everest javascript wrapper
if (EVEREST_ENABLE_JS_SUPPORT)
    add_subdirectory(everestjs)
endif()

# everest python wrapper
if (EVEREST_ENABLE_PY_SUPPORT)
    set(PYTHON_MODULE_EXTENSION ".so")
    add_subdirectory(everestpy)
endif()

if (EVEREST_ENABLE_RS_SUPPORT)
  add_subdirectory(everestrs)
endif()

# FIXME (aw): should this be installed or not?  Right now it is needed for the
#             current packaging approach
install(TARGETS framework
    EXPORT framework-targets
    LIBRARY
)

# packaging
if (FRAMEWORK_INSTALL)
    install(
        DIRECTORY include/
        DESTINATION include/everest
    )

    install(
        FILES ${EVEREST_FRAMEWORK_GENERATED_INC_DIR}/everest/compile_time_settings.hpp
        DESTINATION include/everest
    )

    evc_setup_package(
        NAME everest-framework
        EXPORT framework-targets
        NAMESPACE everest
        ADDITIONAL_CONTENT
            "find_dependency(everest-helpers)"
            "find_dependency(everest-util)"
            "find_dependency(nlohmann_json)"
            "find_dependency(nlohmann_json_schema_validator)"
            "find_dependency(fmt)"
            "find_dependency(date)"
            "set(EVEREST_SCHEMA_DIR \"@PACKAGE_EVEREST_SCHEMA_DIR@\")"
        PATH_VARS
            EVEREST_SCHEMA_DIR "${CMAKE_INSTALL_DATADIR}/everest/schemas"
    )
endif ()

# testing
if(EVEREST_FRAMEWORK_BUILD_TESTING)
    include(CTest)
    add_subdirectory(tests)
else()
    message(STATUS "Not running unit tests")
endif()

# configure clang-tidy if requested
if(CMAKE_RUN_CLANG_TIDY)
    message("Enabling clang-tidy")
    set(CMAKE_CXX_CLANG_TIDY clang-tidy)
endif()

# build doxygen documentation if doxygen is available
find_package(Doxygen)
if(DOXYGEN_FOUND)
    set( DOXYGEN_OUTPUT_DIRECTORY dist/docs )
    doxygen_add_docs(doxygen-${PROJECT_NAME} everest.js include lib src)
else()
    message("Doxygen is needed to generate documentation")
endif()
