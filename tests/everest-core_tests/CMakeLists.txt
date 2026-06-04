project(everest-core_tests
    DESCRIPTION "Extending EVerest with testing modules/interfaces/types/errors.. etc"
    LANGUAGES CXX C)
ev_add_project(SKIP_DOC_GENERATION)

add_subdirectory(config)

add_test(
    NAME manager_framework_transport_integration_test
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/manager_framework_transport_test.py
        --manager $<TARGET_FILE:manager>
        --prefix ${CMAKE_BINARY_DIR}/dist
)
set_tests_properties(manager_framework_transport_integration_test PROPERTIES
    TIMEOUT 60
    LABELS "integration;framework;shm;mqtt"
)
