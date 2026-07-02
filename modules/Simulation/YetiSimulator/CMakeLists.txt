#
# AUTO GENERATED - MARKED REGIONS WILL BE KEPT
# template version 3
#

# module setup:
#   - ${MODULE_NAME}: module name
ev_setup_cpp_module()

# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1
# insert your custom targets and additional config variables here
target_link_libraries(${MODULE_NAME}
    PRIVATE
        everest::everest_api_types
)
# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1

target_sources(${MODULE_NAME}
    PRIVATE
        "powermeter/powermeterImpl.cpp"
        "board_support/evse_board_supportImpl.cpp"
        "ev_board_support/ev_board_supportImpl.cpp"
        "rcd/ac_rcdImpl.cpp"
        "connector_lock/connector_lockImpl.cpp"
)

# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
# insert other things like install cmds etc here
target_sources(${MODULE_NAME}
        PRIVATE
        "util/state.cpp"
        "util/cp_signal.cpp"
        "util/errors.cpp"
        "util/raise_error_router.cpp"
        "util/util.cpp"
)

if (EVEREST_CORE_BUILD_TESTING)
    add_subdirectory(tests)
endif ()
# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
