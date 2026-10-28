# OCPP module common
#
# Support code shared by the OCPP modules (conversions between EVerest and
# OCPP types, transaction handling, error handling/mapping and the EVerest
# device model storage).

add_library(ocpp_module_common STATIC)
add_library(everest::ocpp_module_common ALIAS ocpp_module_common)
ev_register_library_target(ocpp_module_common)

target_sources(ocpp_module_common
    PRIVATE
        src/conversions.cpp
        src/custom_error_mapping.cpp
        src/custom_error_mapping_validation.cpp
        src/error_handling.cpp
        src/error_mapping.cpp
        src/error_placeholders.cpp
        src/transaction_handler.cpp
        src/v16/conversions.cpp
        src/device_model/composed_device_model_storage.cpp
        src/device_model/definitions.cpp
        src/device_model/everest_device_model_storage.cpp
)

target_include_directories(ocpp_module_common
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    "$<BUILD_INTERFACE:$<TARGET_PROPERTY:generate_cpp_files,EVEREST_GENERATED_INCLUDE_DIR>>"
)

target_compile_options(ocpp_module_common
    PRIVATE
        -Wimplicit-fallthrough
        -Werror=switch-enum
)

add_dependencies(ocpp_module_common generate_cpp_files)

# Embed the custom error mapping schema as a build-time C++ string, so validation needs no installed file.
set(CUSTOM_ERROR_MAPPING_SCHEMA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/schemas/custom_error_mapping.schema.json")
file(READ "${CUSTOM_ERROR_MAPPING_SCHEMA_SOURCE}" CUSTOM_ERROR_MAPPING_SCHEMA_JSON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CUSTOM_ERROR_MAPPING_SCHEMA_SOURCE}")
set(CUSTOM_ERROR_MAPPING_SCHEMA_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/custom_error_mapping_schema.cpp")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/custom_error_mapping_schema.cpp.in"
    "${CUSTOM_ERROR_MAPPING_SCHEMA_GENERATED}"
    @ONLY
)
target_sources(ocpp_module_common PRIVATE "${CUSTOM_ERROR_MAPPING_SCHEMA_GENERATED}")

if(DISABLE_EDM)
    find_package(nlohmann_json_schema_validator REQUIRED)
endif()

target_link_libraries(ocpp_module_common
    PUBLIC
        everest::ocpp
        everest::framework
    PRIVATE
        everest::ocpp_conversions
        nlohmann_json_schema_validator::validator
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
