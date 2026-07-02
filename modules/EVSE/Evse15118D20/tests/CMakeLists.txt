set(TEST_TARGET_NAME ${PROJECT_NAME}_evse15118d20_evse_security_chain_mapper_test)

set(MODULE_DIR
    "${PROJECT_SOURCE_DIR}/modules/EVSE/Evse15118D20")

add_executable(${TEST_TARGET_NAME}
    evse_security_chain_mapper_test.cpp
    ${MODULE_DIR}/charger/evse_security_chain_mapper.cpp
)

get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)

target_include_directories(${TEST_TARGET_NAME} PRIVATE
    ${MODULE_DIR}
    ${GENERATED_INCLUDE_DIR}
)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    GTest::gtest_main
    iso15118::iso15118
    everest::framework
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
