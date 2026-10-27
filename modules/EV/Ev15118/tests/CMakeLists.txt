set(TEST_TARGET_NAME ${PROJECT_NAME}_Ev15118_tests)

find_package(OpenSSL 3 REQUIRED)

# FakeController.cpp defines iso15118::ev::Controller in place of the library's, which only works
# while the linker pulls library objects on demand, that is from a static archive.
get_target_property(ISO15118_EV_TYPE everest::iso15118_ev TYPE)
if(NOT ISO15118_EV_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "${TEST_TARGET_NAME} needs everest::iso15118_ev as a static library, not ${ISO15118_EV_TYPE}")
endif()

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
    everest::iso15118_ev
    OpenSSL::Crypto
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
