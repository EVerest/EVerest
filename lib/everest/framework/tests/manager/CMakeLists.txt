set (MANAGER_MODULE_STATUS_TEST "${TEST_TARGET_NAME}_manager_module_status")

# manager_module_status.hpp is header-only, so no source from src/ has to be compiled in here (and
# manager.cpp cannot be, it carries main()).
add_executable(
    ${MANAGER_MODULE_STATUS_TEST}
        test_manager_module_status.cpp
)

target_include_directories(${MANAGER_MODULE_STATUS_TEST}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

# The mapping in manager_module_status.hpp is exhaustive over ManagerState and carries no default
# label, so that adding a state forces its module status to be decided. Make that an actual build
# failure here rather than a convention: this target compiles the header and nothing else, so the
# flag cannot affect unrelated code.
target_compile_options(${MANAGER_MODULE_STATUS_TEST} PRIVATE -Werror=switch)

target_link_libraries(${MANAGER_MODULE_STATUS_TEST}
    PRIVATE
        Catch2::Catch2WithMain
)

catch_discover_tests(${MANAGER_MODULE_STATUS_TEST})
