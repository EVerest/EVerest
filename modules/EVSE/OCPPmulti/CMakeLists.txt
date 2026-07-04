#
# AUTO GENERATED - MARKED REGIONS WILL BE KEPT
# template version 3
#

# module setup:
#   - ${MODULE_NAME}: module name
ev_setup_cpp_module()

# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1
# insert your custom targets and additional config variables here
# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1

target_sources(${MODULE_NAME}
    PRIVATE
        "auth_validator/auth_token_validatorImpl.cpp"
        "auth_provider/auth_token_providerImpl.cpp"
        "data_transfer/ocpp_data_transferImpl.cpp"
        "ocpp_generic/ocppImpl.cpp"
        "session_cost/session_costImpl.cpp"
)

# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
# insert other things like install cmds etc here
target_sources(${MODULE_NAME}
    PRIVATE
    charge_point_config_factory_v16.cpp

    generic_chargepoint.cpp
    generic_ocpp.cpp
    v2_chargepoint.cpp
    v16_chargepoint.cpp
    v16_conversions.cpp
)
target_link_libraries(${MODULE_NAME}
    PRIVATE
    everest::external_energy_limits
    everest::ocpp
    everest::ocpp_conversions
    everest::ocpp_evse_security
    everest::ocpp_module_common
)

if(EVEREST_CORE_BUILD_TESTING)
    add_subdirectory(tests)
endif()
# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
