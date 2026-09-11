set(TEST_TARGET_NAME ${PROJECT_NAME}_YetiSimulator_tests)

add_executable(${TEST_TARGET_NAME}
    EvBoardSupportTest.cpp
    ../ev_board_support/ev_board_supportImpl.cpp
    ../util/state.cpp
)

get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)
add_dependencies(${TEST_TARGET_NAME} ${MODULE_NAME})
target_include_directories(${TEST_TARGET_NAME} PRIVATE
    ..
    ${PROJECT_SOURCE_DIR}/tests/include
    ${GENERATED_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}/generated/modules/${MODULE_NAME}
)
target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    GTest::gtest_main
    everest::framework
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
