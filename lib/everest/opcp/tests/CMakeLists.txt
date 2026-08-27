set(TEST_TARGET_NAME ${PROJECT_NAME}_tests)
add_executable(${TEST_TARGET_NAME})

target_sources(${TEST_TARGET_NAME} PRIVATE
    test_types_environment.cpp
    test_pkcs.cpp
    test_clients.cpp
    test_enroller.cpp
)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    everest::opcp
    GTest::gtest_main
)

set(OPCP_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}")

add_test(
    NAME ${TEST_TARGET_NAME}
    COMMAND ${TEST_TARGET_NAME}
    WORKING_DIRECTORY "${OPCP_TEST_DIR}"
)

file(COPY
    "${CMAKE_CURRENT_SOURCE_DIR}/generate_test_pki.sh"
    DESTINATION "${OPCP_TEST_DIR}"
)
