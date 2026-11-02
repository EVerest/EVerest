set(TEST_TARGET_NAME ${PROJECT_NAME}_InfyPower_BEG1K075G_tests)

add_executable(${TEST_TARGET_NAME}
    InfyCanDevice_test.cpp
    ../can_driver_acdc/InfyCanDevice.cpp
    ../can_driver_acdc/CanPackets.cpp
)

target_include_directories(${TEST_TARGET_NAME} PRIVATE
    ../can_driver_acdc
)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    can_device
    Pal::Sigslot
    everest::log
    GTest::gtest_main
)

target_compile_features(${TEST_TARGET_NAME} PRIVATE cxx_std_17)

add_test(NAME ${TEST_TARGET_NAME} COMMAND ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
