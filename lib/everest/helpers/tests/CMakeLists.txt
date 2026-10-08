set(TEST_TARGET_NAME ${PROJECT_NAME}_helpers_tests)

add_executable(${TEST_TARGET_NAME}
helpers_test.cpp
)

target_include_directories(${TEST_TARGET_NAME}
    PRIVATE
        "$<TARGET_PROPERTY:generate_cpp_files,EVEREST_GENERATED_INCLUDE_DIR>"
)

target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
        GTest::gmock_main
        GTest::gtest_main
        everest::helpers
        nlohmann_json::nlohmann_json
)

include(GoogleTest)
gtest_discover_tests(${TEST_TARGET_NAME})

ev_register_test_target(${TEST_TARGET_NAME})

add_executable(clockshift_probe clockshift_probe.cpp)
target_compile_features(clockshift_probe PRIVATE cxx_std_17)

set(CLOCKSHIFT_TEST_TARGET_NAME ${PROJECT_NAME}_clockshift_tests)
add_executable(${CLOCKSHIFT_TEST_TARGET_NAME} clockshift_test.cpp)
target_compile_features(${CLOCKSHIFT_TEST_TARGET_NAME} PRIVATE cxx_std_17)
target_compile_definitions(${CLOCKSHIFT_TEST_TARGET_NAME}
    PRIVATE
        CLOCKSHIFT_LIB="$<TARGET_FILE:everest_clockshift>"
        CLOCKSHIFT_PROBE="$<TARGET_FILE:clockshift_probe>"
)
target_link_libraries(${CLOCKSHIFT_TEST_TARGET_NAME}
    PRIVATE
        GTest::gtest_main
)
add_dependencies(${CLOCKSHIFT_TEST_TARGET_NAME} everest_clockshift clockshift_probe)
gtest_discover_tests(${CLOCKSHIFT_TEST_TARGET_NAME})

ev_register_test_target(${CLOCKSHIFT_TEST_TARGET_NAME})
