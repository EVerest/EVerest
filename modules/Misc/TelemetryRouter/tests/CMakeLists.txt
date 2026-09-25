set(TARGET_NAME ${PROJECT_NAME}_module_telemetry_router_tests)
add_executable(${TARGET_NAME})

target_sources(${TARGET_NAME}
    PRIVATE
        test_catalog.cpp
        test_ocpp_sink.cpp
        test_receiver.cpp
        test_router.cpp
        test_rules.cpp
        test_value_format.cpp
)

if (TELEMETRY_ROUTER_WITH_OTEL)
    target_sources(${TARGET_NAME} PRIVATE test_otel_sink.cpp)
endif()

target_include_directories(${TARGET_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib
)

target_compile_definitions(${TARGET_NAME}
    PRIVATE
        TELEMETRY_ROUTER_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../config"
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        TelemetryRouter_core
        everest::yaml
        Catch2::Catch2WithMain
)

if(NOT DISABLE_EDM)
    list(APPEND CMAKE_MODULE_PATH ${CPM_PACKAGE_catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(${TARGET_NAME})
endif()

add_test(${TARGET_NAME} ${TARGET_NAME})
ev_register_test_target(${TARGET_NAME})
