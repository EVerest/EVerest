# V2G telemetry payload structs + JSON converters.
# Consumed by EvseV2G (publisher) and downstream telemetry subscribers
# (e.g. the Tritium ccu_rtm75_everest rte shim).

add_library(everest_v2g_telemetry STATIC)
add_library(everest::v2g_telemetry ALIAS everest_v2g_telemetry)
ev_register_library_target(everest_v2g_telemetry)

target_sources(everest_v2g_telemetry
    PRIVATE
        src/telemetry.cpp
)

target_include_directories(everest_v2g_telemetry
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(everest_v2g_telemetry
    PUBLIC
        nlohmann_json::nlohmann_json
)
