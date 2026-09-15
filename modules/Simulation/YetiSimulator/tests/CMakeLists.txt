# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

# Two suites live side by side because they use different frameworks and so
# cannot share one executable: the board-support tests are GoogleTest, the
# error-routing tests are Catch2.

set(TEST_TARGET_NAME ${PROJECT_NAME}_YetiSimulator_tests)

add_executable(${TEST_TARGET_NAME}
    EvBoardSupportTest.cpp
    ../ev_board_support/ev_board_supportImpl.cpp
    ../util/state.cpp
)

get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)
add_dependencies(${TEST_TARGET_NAME} ${MODULE_NAME})
target_include_directories(${TEST_TARGET_NAME} PRIVATE
    ..
    ${PROJECT_SOURCE_DIR}/tests/include
    ${GENERATED_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}/generated/modules/${MODULE_NAME}
)
target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    GTest::gtest_main
    everest::framework
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})

set(ERROR_TEST_TARGET ${PROJECT_NAME}_YetiSimulator_error_tests)
add_executable(${ERROR_TEST_TARGET})

# Depend on module build so ev-cli has generated Conf + Intf headers.
add_dependencies(${ERROR_TEST_TARGET} ${MODULE_NAME})
target_include_directories(${ERROR_TEST_TARGET} PRIVATE
    ..                                                   # module root
    ${GENERATED_INCLUDE_DIR}                             # framework-generated common headers
    ${CMAKE_BINARY_DIR}/generated/modules/${MODULE_NAME} # module-specific generated headers
)

target_sources(${ERROR_TEST_TARGET} PRIVATE
    RaiseErrorRouterTest.cpp
    DiodeFaultDetectionTest.cpp
    ../util/cp_signal.cpp
    ../util/errors.cpp
    ../util/raise_error_router.cpp
)

target_link_libraries(${ERROR_TEST_TARGET}
    PRIVATE
        everest::framework
        everest::log
        everest::util
        everest::everest_api_types
        Catch2::Catch2WithMain
        Threads::Threads
)

if (NOT DISABLE_EDM)
    list(APPEND CMAKE_MODULE_PATH ${CPM_PACKAGE_catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(${ERROR_TEST_TARGET})
endif ()

add_test(${ERROR_TEST_TARGET} ${ERROR_TEST_TARGET})
ev_register_test_target(${ERROR_TEST_TARGET})
