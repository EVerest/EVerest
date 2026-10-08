#
# AUTO GENERATED - MARKED REGIONS WILL BE KEPT
# template version 3
#

# module setup:
#   - ${MODULE_NAME}: module name
ev_setup_cpp_module()

# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1
find_package(SQLite3 REQUIRED)
if(NOT TARGET SQLite3::SQLite3)
  set_target_properties(SQLite::SQLite3 PROPERTIES IMPORTED_GLOBAL TRUE)
  add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()

include(${everest-sqlite_SOURCE_DIR}/cmake/CollectMigrationFiles.cmake)

collect_migration_files(
    LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/migrations
    INSTALL_DESTINATION ${CMAKE_INSTALL_DATADIR}/everest/modules/${MODULE_NAME}/migrations
)

# TARGET_MIGRATION_FILE_VERSION is also a PUBLIC compile definition of everest::framework,
# so the module needs its own macro name for its value
target_compile_definitions(${MODULE_NAME}
    PRIVATE
        SESSION_STORAGE_MIGRATION_FILE_VERSION=${TARGET_MIGRATION_FILE_VERSION}
)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        SQLite3::SQLite3
        everest::sqlite
        everest::tls
        everest::util
)

target_sources(${MODULE_NAME}
    PRIVATE
        "storage/record_conversions.cpp"
        "storage/session_recorder.cpp"
        "storage/session_store.cpp"
)
# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1

target_sources(${MODULE_NAME}
    PRIVATE
        "main/session_storageImpl.cpp"
)

# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
if(EVEREST_CORE_BUILD_TESTING)
    add_subdirectory(tests)
endif()
# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
