# Certificate-store telemetry payload structs + JSON converters.
# Published by EvseSecurity on cert-store mutations; consumed by downstream
# telemetry subscribers (Tritium ccu_rtm75_everest rte shim).

add_library(everest_cert_telemetry STATIC)
add_library(everest::cert_telemetry ALIAS everest_cert_telemetry)
ev_register_library_target(everest_cert_telemetry)

target_sources(everest_cert_telemetry
    PRIVATE
        src/telemetry.cpp
)

target_include_directories(everest_cert_telemetry
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(everest_cert_telemetry
    PUBLIC
        nlohmann_json::nlohmann_json
)
