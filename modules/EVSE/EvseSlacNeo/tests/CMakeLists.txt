set(TEST_TARGET_NAME ${PROJECT_NAME}_evse_slac_neo_module_tests)

add_executable(${TEST_TARGET_NAME}
    lifecycle_gate_tests.cpp
)

target_include_directories(${TEST_TARGET_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../main"
)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    GTest::gtest_main
    everest::util
)

target_compile_features(${TEST_TARGET_NAME} PRIVATE cxx_std_17)

add_test(NAME ${TEST_TARGET_NAME} COMMAND ${TEST_TARGET_NAME})
