file(GLOB_RECURSE LIB_SOURCES "lib/*.cpp")
add_library(power_stack_mock_lib STATIC ${LIB_SOURCES})
target_include_directories(power_stack_mock_lib PUBLIC include)

target_link_libraries(power_stack_mock_lib
    PUBLIC
    fusion_charger_dispenser
    modbus-ssl
    modbus-client
    atomic
)

# everest::io is only available when building inside EVerest
if(NOT BUILDING_IN_EVEREST OR NOT EVEREST_IO_WITH_MQTT)
    message(STATUS "everest::io with MQTT support not available, not building fusion_charger_mock")
    return()
endif()

file (GLOB_RECURSE EXECUTABLE_SOURCES "src/*.cpp")

add_executable(fusion_charger_mock ${EXECUTABLE_SOURCES})
target_link_libraries(fusion_charger_mock power_stack_mock_lib)
target_link_libraries(fusion_charger_mock atomic everest::io)
