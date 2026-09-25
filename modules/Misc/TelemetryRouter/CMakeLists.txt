#
# AUTO GENERATED - MARKED REGIONS WILL BE KEPT
# template version 3
#

# module setup:
#   - ${MODULE_NAME}: module name
ev_setup_cpp_module()

# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1
add_library(${MODULE_NAME}_core STATIC
    lib/catalog.cpp
    lib/log_sink.cpp
    lib/ocpp_sink.cpp
    lib/receiver.cpp
    lib/router.cpp
    lib/rules.cpp
    lib/sink_factories.cpp
    lib/value_format.cpp
)
set_target_properties(${MODULE_NAME}_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(${MODULE_NAME}_core PUBLIC cxx_std_17)
target_link_libraries(${MODULE_NAME}_core
    PUBLIC
        everest::framework
        everest::io
        everest::log
        fmt::fmt
)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        ${MODULE_NAME}_core
        everest::yaml
)
# ev@bcc62523-e22b-41d7-ba2f-825b493a3c97:v1

target_sources(${MODULE_NAME}
    PRIVATE
        "main/emptyImpl.cpp"
)

# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
install(
    FILES
        config/telemetry_rules.yaml
        config/telemetry_rules.example.yaml
        config/component_config/Telemetry_example.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/everest/modules/${MODULE_NAME}
)

if(EVEREST_CORE_BUILD_TESTING)
    add_subdirectory(tests)
endif()
# ev@c55432ab-152c-45a9-9d2e-7281d50c69c3:v1
