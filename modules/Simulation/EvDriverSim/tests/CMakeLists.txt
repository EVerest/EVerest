# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
set(TEST_TARGET ${PROJECT_NAME}_EvDriverSim_tests)
add_executable(${TEST_TARGET}
    PilotGateTest.cpp
    WorldFaultsTest.cpp
    ModuleTest.cpp
    ScenarioRunnerTest.cpp
    ../main/PilotGate.cpp
    ../main/ScenarioRunner.cpp
    ../main/WorldFaults.cpp
    ../EvDriverSim.cpp
    ../board_support/ev_board_supportImpl.cpp
)
add_dependencies(${TEST_TARGET} ${MODULE_NAME})
get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)
target_include_directories(${TEST_TARGET} SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/tests/include)
target_include_directories(${TEST_TARGET} PRIVATE .. ${GENERATED_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}/generated/modules/${MODULE_NAME})
target_link_libraries(${TEST_TARGET} PRIVATE everest::framework everest::everest_api_types everest::util Catch2::Catch2WithMain)
if (NOT DISABLE_EDM)
    list(APPEND CMAKE_MODULE_PATH ${CPM_PACKAGE_catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(${TEST_TARGET})
endif ()
add_test(${TEST_TARGET} ${TEST_TARGET})
ev_register_test_target(${TEST_TARGET})
