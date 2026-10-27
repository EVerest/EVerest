#
# AUTO GENERATED - MARKED REGIONS WILL BE KEPT
# template version 3
#

# module setup:
#   - ${MODULE_NAME}: module name
ev_setup_cpp_module()

# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1
target_link_libraries(${MODULE_NAME}
    PRIVATE
        everest::iso15118_ev
        everest::util
        everest::iso15118_conversions
)

# The enum conversions are exhaustive switches; a new enumerator must fail the build.
target_compile_options(${MODULE_NAME} PRIVATE -Werror=switch)
# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1

target_sources(${MODULE_NAME}
    PRIVATE
        "ev/ISO15118_evImpl.cpp"
)

# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
target_sources(${MODULE_NAME}
    PRIVATE
        "ev/contract_store.cpp"
        "ev/der_publish_names.cpp"
        "ev/sae_profile_config.cpp"
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
