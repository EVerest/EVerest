set(TEST_TARGET_NAME ${PROJECT_NAME}_charx_power_ds_tests)

set(MODULE_DIR "${PROJECT_SOURCE_DIR}/modules/HardwareDrivers/PowerSupplies/CharxPowerDS")

add_executable(${TEST_TARGET_NAME}
        test_charx_canopen.cpp
        test_charx_controller.cpp
        "${MODULE_DIR}/main/charx_canopen.cpp"
        "${MODULE_DIR}/main/charx_controller.cpp"
)
add_dependencies(${TEST_TARGET_NAME} ${MODULE_NAME})

target_include_directories(${TEST_TARGET_NAME} PRIVATE
        "${MODULE_DIR}/main"
        "${MODULE_DIR}/tests"
)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
        GTest::gmock_main
        everest::log
        fmt::fmt
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
