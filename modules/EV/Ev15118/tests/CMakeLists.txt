set(TEST_TARGET_NAME ${PROJECT_NAME}_Ev15118_tests)

add_executable(${TEST_TARGET_NAME})

target_include_directories(${TEST_TARGET_NAME} PRIVATE ../ev)

target_sources(${TEST_TARGET_NAME} PRIVATE
    SaeProfileConfigTest.cpp
    ../ev/sae_profile_config.cpp
)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    GTest::gmock
    GTest::gtest_main
    everest::framework
    iso15118::iso15118
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
