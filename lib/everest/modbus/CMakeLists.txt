add_subdirectory(libs)
add_subdirectory(examples)

# The Huawei fusion charger library can also build this directory standalone, outside EVerest
if(COMMAND ev_register_library_target)
    ev_register_library_target(modbus-base)
    ev_register_library_target(modbus-server)
    ev_register_library_target(modbus-client)
    ev_register_library_target(modbus-ssl)
    ev_register_library_target(modbus-registers)
endif()
