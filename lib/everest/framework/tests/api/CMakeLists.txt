set(API_TEST "${TEST_TARGET_NAME}_api")

add_executable(
    ${API_TEST}
        test_configuration_api_handlers.cpp
        test_lifecycle_api_handlers.cpp
        test_configuration_type_wrapper.cpp
)

target_include_directories(${API_TEST}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(${API_TEST}
    PRIVATE
        Catch2::Catch2WithMain
        everest::framework
        everest::everest_api_types
        management_api
        nlohmann_json::nlohmann_json
        ryml::ryml
)

catch_discover_tests(${API_TEST})
