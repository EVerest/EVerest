set(TEST_TARGET_NAME ${PROJECT_NAME}_persistent_session_storage_tests)

add_executable(${TEST_TARGET_NAME})

target_sources(${TEST_TARGET_NAME}
    PRIVATE
        record_conversions_tests.cpp
        session_recorder_tests.cpp
        session_store_tests.cpp
        ../storage/record_conversions.cpp
        ../storage/session_recorder.cpp
        ../storage/session_store.cpp
)

get_target_property(GENERATED_INCLUDE_DIR generate_cpp_files EVEREST_GENERATED_INCLUDE_DIR)

target_include_directories(${TEST_TARGET_NAME}
    PRIVATE
        ..
        ${everest-sqlite_SOURCE_DIR}/tests
        ${GENERATED_INCLUDE_DIR}
)

target_compile_definitions(${TEST_TARGET_NAME}
    PRIVATE
        SESSION_STORAGE_MIGRATION_FILE_VERSION=${TARGET_MIGRATION_FILE_VERSION}
        MIGRATION_FILES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../migrations"
)

target_link_libraries(${TEST_TARGET_NAME}
    PRIVATE
        everest::sqlite
        everest::log
        everest::tls
        everest::util
        SQLite3::SQLite3
        nlohmann_json::nlohmann_json
        GTest::gmock_main
)

add_dependencies(${TEST_TARGET_NAME} generate_cpp_files)

add_test(${TEST_TARGET_NAME} ${TEST_TARGET_NAME})
ev_register_test_target(${TEST_TARGET_NAME})
