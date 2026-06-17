set(TEST_TARGET_NAME ${PROJECT_NAME}_derating_curve_tests)

add_executable(${TEST_TARGET_NAME}
    test_derating_curve.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../derating/derating_curve.cpp
)

target_include_directories(${TEST_TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
