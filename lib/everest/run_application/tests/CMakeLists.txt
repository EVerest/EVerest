add_executable(everest_run_application_tests
    run_application_test.cpp
)
target_link_libraries(everest_run_application_tests
    PRIVATE
        GTest::gtest_main
        everest::run_application
)
include(GoogleTest)
gtest_discover_tests(everest_run_application_tests TEST_PREFIX "run_application.")
