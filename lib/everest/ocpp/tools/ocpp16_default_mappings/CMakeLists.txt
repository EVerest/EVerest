if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    message(STATUS "Cross-compiling without CMAKE_CROSSCOMPILING_EMULATOR: skipping default_ocpp-16_mappings.yaml")
    return()
endif()

# Built from the libocpp sources it needs rather than linked to ocpp, so generating the file
# does not build the whole library and its dependencies.
set(LIBOCPP_LIB_PATH ${PROJECT_SOURCE_DIR}/lib/ocpp)
add_executable(ocpp16_default_mappings_generator
    main.cpp
    ${LIBOCPP_LIB_PATH}/common/json_number.cpp
    ${LIBOCPP_LIB_PATH}/common/types.cpp
    ${LIBOCPP_LIB_PATH}/common/utils.cpp
    ${LIBOCPP_LIB_PATH}/v16/known_keys.cpp
    ${LIBOCPP_LIB_PATH}/v16/known_keys_yaml.cpp
    ${LIBOCPP_LIB_PATH}/v16/ocpp_enums.cpp
    ${LIBOCPP_LIB_PATH}/v2/ctrlr_component_variables.cpp
    ${LIBOCPP_LIB_PATH}/v2/ocpp_enums.cpp
    ${LIBOCPP_LIB_PATH}/v2/ocpp_types.cpp
)
target_include_directories(ocpp16_default_mappings_generator
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/3rd_party
)
target_link_libraries(ocpp16_default_mappings_generator
    PRIVATE
        nlohmann_json::nlohmann_json
        ryml::ryml
        date::date-tz
        everest::timer
)
if(EVEREST_CUSTOM_LOGGING_LIBRARY)
    target_link_libraries(ocpp16_default_mappings_generator PRIVATE ${EVEREST_CUSTOM_LOGGING_LIBRARY})
elseif(EVEREST_CUSTOM_LOGGING_INCLUDE_PATH)
    target_include_directories(ocpp16_default_mappings_generator PRIVATE ${EVEREST_CUSTOM_LOGGING_INCLUDE_PATH})
else()
    target_link_libraries(ocpp16_default_mappings_generator PRIVATE everest::log)
endif()
target_compile_features(ocpp16_default_mappings_generator PRIVATE cxx_std_17)

set(OCPP16_DEFAULT_MAPPINGS_FILE "${CMAKE_CURRENT_BINARY_DIR}/default_ocpp-16_mappings.yaml")

add_custom_command(
    OUTPUT ${OCPP16_DEFAULT_MAPPINGS_FILE}
    COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:ocpp16_default_mappings_generator> ${OCPP16_DEFAULT_MAPPINGS_FILE}
    DEPENDS ocpp16_default_mappings_generator
    COMMENT "Generating default_ocpp-16_mappings.yaml"
)
add_custom_target(ocpp16_default_mappings_yaml ALL DEPENDS ${OCPP16_DEFAULT_MAPPINGS_FILE})

install(
    FILES ${OCPP16_DEFAULT_MAPPINGS_FILE}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/everest/modules/OCPP
)
