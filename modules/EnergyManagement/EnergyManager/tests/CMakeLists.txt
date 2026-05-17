set(TEST_TARGET_NAME ${PROJECT_NAME}_EnergyManager_tests)
add_executable(${TEST_TARGET_NAME})

add_dependencies(${TEST_TARGET_NAME} ${MODULE_NAME})

get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)

target_include_directories(${TEST_TARGET_NAME} PRIVATE
    ..
    ${GENERATED_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}/generated/modules/${MODULE_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_sources(${TEST_TARGET_NAME} PRIVATE
    energy_manager_tests.cpp
    JsonDefinedEnergyManagerTest.cpp
    ../Broker.cpp
    ../BrokerFastCharging.cpp
    ../EnergyManagerImpl.cpp
    ../Market.cpp
    ../Offer.cpp
)

set(JSON_TESTS_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/json_tests")

target_compile_definitions(${TEST_TARGET_NAME} PRIVATE
    BUILD_TESTING_MODULE_ENERGY_MANAGER
    JSON_TESTS_LOCATION="${JSON_TESTS_LOCATION}"
)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    GTest::gmock
    GTest::gtest_main
    everest::log
    everest::framework
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})

# Copy the json files used for testing to the destination directory.
# Uses a stamp file so the copy only runs when source fixtures change —
# plain add_custom_target always runs, which invalidates dependents on every build.
# NOTE: glob is intentionally NOT CONFIGURE_DEPENDS — that flag triggers a cmake
# reconfigure on any fixture mtime change, which cascades into ev-cli regeneration
# and rebuilds hundreds of unrelated targets. If you add/remove a fixture file,
# re-run cmake manually.
file(GLOB JSON_TEST_FILES "${CMAKE_CURRENT_SOURCE_DIR}/json_tests/*")
set(JSON_TESTS_STAMP "${CMAKE_CURRENT_BINARY_DIR}/json_tests.stamp")

add_custom_command(
    OUTPUT ${JSON_TESTS_STAMP}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/json_tests
            ${CMAKE_CURRENT_BINARY_DIR}/json_tests
    COMMAND ${CMAKE_COMMAND} -E touch ${JSON_TESTS_STAMP}
    DEPENDS ${JSON_TEST_FILES}
    VERBATIM
)

add_custom_target(copy_json_tests DEPENDS ${JSON_TESTS_STAMP})

add_dependencies(${TEST_TARGET_NAME} copy_json_tests)
