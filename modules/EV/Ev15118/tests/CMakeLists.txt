set(TEST_TARGET_NAME ${PROJECT_NAME}_Ev15118_tests)

find_package(OpenSSL 3 REQUIRED)

add_executable(${TEST_TARGET_NAME})

add_dependencies(${TEST_TARGET_NAME} ${MODULE_NAME})

get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)

target_include_directories(${TEST_TARGET_NAME} PRIVATE
    ../ev
    ${GENERATED_INCLUDE_DIR}
    ${CMAKE_BINARY_DIR}/generated/modules/${MODULE_NAME}
)

target_sources(${TEST_TARGET_NAME} PRIVATE
    ConfigDefaultsTest.cpp
    ContractStoreTest.cpp
    DerPublishNamesTest.cpp
    FakeController.cpp
    SaeProfileConfigTest.cpp
    SessionHandoffTest.cpp
    SessionParamsTest.cpp
    SessionPublicationsTest.cpp
    test_contract.cpp
    ../ev/ISO15118_evImpl.cpp
    ../ev/contract_store.cpp
    ../ev/der_publish_names.cpp
    ../ev/sae_profile_config.cpp
)

target_compile_definitions(${TEST_TARGET_NAME} PRIVATE EVEREST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_compile_options(${TEST_TARGET_NAME} PRIVATE -Werror=switch)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    GTest::gmock
    GTest::gtest_main
    everest::framework
    everest::iso15118_conversions
    everest::util
    everest::iso15118
    OpenSSL::Crypto
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
