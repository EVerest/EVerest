set(TEST_TARGET_NAME ${PROJECT_NAME}_iso15118_ev_session_tests)

add_executable(${TEST_TARGET_NAME}
    der_publish_names_test.cpp
    sae_profile_config_test.cpp
    session_setup_test.cpp
)

target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
        GTest::gmock
        GTest::gtest_main
        everest::iso15118_ev_session
)

include(GoogleTest)
gtest_discover_tests(${TEST_TARGET_NAME})

ev_register_test_target(${TEST_TARGET_NAME})
