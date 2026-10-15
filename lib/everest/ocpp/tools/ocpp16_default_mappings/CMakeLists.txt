if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    message(STATUS "Cross-compiling without CMAKE_CROSSCOMPILING_EMULATOR: skipping default_ocpp-16_mappings.yaml")
    return()
endif()

add_executable(ocpp16_default_mappings_generator main.cpp)
target_link_libraries(ocpp16_default_mappings_generator PRIVATE ocpp)

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
