# EvseManager control-status telemetry payload struct + JSON converters.
# Published by EvseManager from session lifecycle + auth/payment config.

add_library(everest_evse_control_telemetry STATIC)
add_library(everest::evse_control_telemetry ALIAS everest_evse_control_telemetry)
ev_register_library_target(everest_evse_control_telemetry)

target_sources(everest_evse_control_telemetry
    PRIVATE
        src/telemetry.cpp
)

target_include_directories(everest_evse_control_telemetry
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(everest_evse_control_telemetry
    PUBLIC
        nlohmann_json::nlohmann_json
)
