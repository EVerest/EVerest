set(TEST_TARGET_NAME ${PROJECT_NAME}_types_tests)

add_executable(${TEST_TARGET_NAME})

# The generated type headers (generated/types/*.hpp) are produced by
# ev_add_project() via the `generate_cpp_files` target.
add_dependencies(${TEST_TARGET_NAME} generate_cpp_files)

get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)

target_include_directories(${TEST_TARGET_NAME} PRIVATE
    ${GENERATED_INCLUDE_DIR}
)

target_sources(${TEST_TARGET_NAME} PRIVATE
    enum_serialization_test.cpp
)

target_compile_features(${TEST_TARGET_NAME} PRIVATE cxx_std_17)

target_link_libraries(${TEST_TARGET_NAME} PRIVATE
    Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json
)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
