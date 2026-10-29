add_executable(everest_io_tests
  mdns_test.cpp
)

target_link_libraries(everest_io_tests
  PRIVATE
        GTest::gtest_main
        everest::io
)

include(GoogleTest)
gtest_discover_tests(everest_io_tests TEST_PREFIX "io.")
