set(TEST_TARGET_NAME ${PROJECT_NAME}_mcs_ev_data_link_module_tests)

add_executable(${TEST_TARGET_NAME}
    link_state_machine_tests.cpp
    ../main/link_state_machine.cpp
)

target_include_directories(${TEST_TARGET_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../main"
)

# The state machine is the only thing under test here and it needs nothing but boost::msm: the
# netlink components and the lifecycle gate belong to the controller, which is not in this target.
target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    GTest::gtest_main
    Boost::headers
)

target_compile_features(${TEST_TARGET_NAME} PRIVATE cxx_std_17)

add_test(NAME ${TEST_TARGET_NAME} COMMAND ${TEST_TARGET_NAME})
