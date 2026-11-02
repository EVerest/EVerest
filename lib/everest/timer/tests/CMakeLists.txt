set(TEST_TARGET_NAME ${PROJECT_NAME}_tests)
add_executable(${TEST_TARGET_NAME} libtimer_unit_test.cpp)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
        GTest::gtest_main
        everest::timer
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
