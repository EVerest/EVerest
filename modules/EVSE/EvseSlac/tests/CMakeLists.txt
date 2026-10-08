set(TEST_TARGET_NAME ${PROJECT_NAME}_evse_slac_module_tests)

add_executable(${TEST_TARGET_NAME}
    lifecycle_gate_tests.cpp
    rx_dispatch_tests.cpp
    command_path_tests.cpp
    amp_map_file_tests.cpp
    slac_runtime_tests.cpp
    ../main/amp_map_file.cpp
    ../main/slac_runtime.cpp
    # The receive-dispatch tests drive the real controller, so its translation unit is compiled
    # into the test binary. slacImpl.cpp cannot be: it needs the generated framework headers.
    ../main/fsm_controller.cpp
)

target_include_directories(${TEST_TARGET_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../main"
)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    GTest::gtest_main
    everest::util
    slac::slac
    everest::io
    everest::yaml
    nlohmann_json::nlohmann_json
    fmt::fmt
)

target_compile_features(${TEST_TARGET_NAME} PRIVATE cxx_std_17)

add_test(NAME ${TEST_TARGET_NAME} COMMAND ${TEST_TARGET_NAME})
