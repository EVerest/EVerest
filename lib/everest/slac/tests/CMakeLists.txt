set(TEST_TARGET_NAME slac_unit_test)
add_executable(${TEST_TARGET_NAME} libslac_unit_test.cpp)

target_include_directories(${TEST_TARGET_NAME} PUBLIC${GTEST_INCLUDE_DIRS})

if(DISABLE_EDM)
    find_package(GTest REQUIRED)
else()
    set(GTEST_LIBRARIES
        GTest::gmock
        GTest::gtest
        GTest::gtest_main
    )
endif()

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
        ${GTEST_LIBRARIES}
        slac::slac
)

include(GoogleTest)
gtest_discover_tests(${TEST_TARGET_NAME})

ev_register_test_target(${TEST_TARGET_NAME})

set(EVSE_MATCHING_TEST_TARGET_NAME slac_evse_matching_sessions_test)
add_executable(${EVSE_MATCHING_TEST_TARGET_NAME} evse_matching_sessions_test.cpp)

target_link_libraries(${EVSE_MATCHING_TEST_TARGET_NAME} PRIVATE
        ${GTEST_LIBRARIES}
        slac::fsm::evse
)

gtest_discover_tests(${EVSE_MATCHING_TEST_TARGET_NAME})

ev_register_test_target(${EVSE_MATCHING_TEST_TARGET_NAME})
