# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Contributors to EVerest

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(everest_thread_pool_scaling_reliability LANGUAGES CXX)
    include(CTest)

    set(EVEREST_GTEST_SOURCE_DIR "" CACHE PATH "GoogleTest source directory used when no installed package is available")
    find_package(GTest CONFIG QUIET)
    if(NOT TARGET GTest::gtest_main)
        if(EVEREST_GTEST_SOURCE_DIR)
            add_subdirectory(${EVEREST_GTEST_SOURCE_DIR} ${CMAKE_BINARY_DIR}/googletest EXCLUDE_FROM_ALL)
        else()
            message(FATAL_ERROR
                "GTest was not found. Set EVEREST_GTEST_SOURCE_DIR to an existing GoogleTest source directory.")
        endif()
    endif()
    set(EVEREST_UTIL_RELIABILITY_LIBRARIES "")
else()
    set(EVEREST_UTIL_RELIABILITY_LIBRARIES everest::util)
endif()

find_package(Threads REQUIRED)
option(EVEREST_UTIL_RELIABILITY_FAULT_PROBES "Build Linux-only fault-injection probe binaries" ON)

add_executable(everest_thread_pool_scaling_reliability_tests
    thread_pool_scaling_reliability_tests.cpp
)
target_compile_features(everest_thread_pool_scaling_reliability_tests PRIVATE cxx_std_17)
target_include_directories(everest_thread_pool_scaling_reliability_tests PRIVATE ../../include)
target_link_libraries(everest_thread_pool_scaling_reliability_tests
    PRIVATE
        GTest::gtest_main
        ${EVEREST_UTIL_RELIABILITY_LIBRARIES}
)

include(GoogleTest)
gtest_discover_tests(everest_thread_pool_scaling_reliability_tests
    PROPERTIES
        LABELS "reliability"
        TIMEOUT 30
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EVEREST_UTIL_RELIABILITY_FAULT_PROBES)
    set(EVEREST_THREAD_POOL_PROBES
        thread_pool_allocation_probe
        thread_pool_cold_chain_probe
        thread_pool_drain_bursts_probe
        thread_pool_faults_probe
        thread_pool_late_submit_probe
        thread_pool_liveness_probe
        thread_pool_shutdown_probe
        thread_pool_stress_fault_probe
    )

    foreach(EVEREST_THREAD_POOL_PROBE IN LISTS EVEREST_THREAD_POOL_PROBES)
        add_executable(${EVEREST_THREAD_POOL_PROBE} probes/${EVEREST_THREAD_POOL_PROBE}.cpp)
        target_compile_features(${EVEREST_THREAD_POOL_PROBE} PRIVATE cxx_std_17)
        target_include_directories(${EVEREST_THREAD_POOL_PROBE} PRIVATE ../../include)
        target_link_libraries(${EVEREST_THREAD_POOL_PROBE}
            PRIVATE
                ${EVEREST_UTIL_RELIABILITY_LIBRARIES}
                Threads::Threads
                ${CMAKE_DL_LIBS}
        )
    endforeach()

    add_executable(everest_thread_pool_scaling_probe_driver thread_pool_scaling_probe_driver.cpp)
    target_compile_features(everest_thread_pool_scaling_probe_driver PRIVATE cxx_std_17)
    target_link_libraries(everest_thread_pool_scaling_probe_driver PRIVATE GTest::gtest_main)
    add_dependencies(everest_thread_pool_scaling_probe_driver ${EVEREST_THREAD_POOL_PROBES})

    gtest_discover_tests(everest_thread_pool_scaling_probe_driver
        PROPERTIES
            LABELS "fault-injection"
            RUN_SERIAL TRUE
            TIMEOUT 120
    )
endif()
