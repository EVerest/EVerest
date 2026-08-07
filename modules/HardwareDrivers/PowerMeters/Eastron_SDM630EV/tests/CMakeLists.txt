set(TEST_TARGET_NAME ${PROJECT_NAME}_eastron_sdm630ev_tests)

set(MODULE_DIR "${PROJECT_SOURCE_DIR}/modules/HardwareDrivers/PowerMeters/Eastron_SDM630EV")

add_executable(${TEST_TARGET_NAME}
        test_sdm630ev_helper.cpp
        test_sdm630ev_powermeter_impl.cpp
        ${MODULE_DIR}/main/powermeterImpl.cpp
        ${MODULE_DIR}/main/transport.cpp
)

add_dependencies(${TEST_TARGET_NAME} ${MODULE_NAME})

get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)

target_include_directories(${TEST_TARGET_NAME} PRIVATE
        tests
        "${MODULE_DIR}/main"
        "${MODULE_DIR}/tests"
        ${GENERATED_INCLUDE_DIR}
        ${CMAKE_BINARY_DIR}/generated/modules/Eastron_SDM630EV
)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
        GTest::gtest_main
        everest::framework
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
