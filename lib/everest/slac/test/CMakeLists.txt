add_executable(evse_slac_set_key_test
    evse_slac_set_key_test.cpp
    slac_fsm_type_traits_test.cpp
)

target_link_libraries(evse_slac_set_key_test
    PRIVATE
        slac
        nlohmann_json::nlohmann_json
)

target_compile_features(evse_slac_set_key_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_set_key_test COMMAND evse_slac_set_key_test)

add_executable(evse_slac_reset_chip_test evse_slac_reset_chip_test.cpp)

target_link_libraries(evse_slac_reset_chip_test
    PRIVATE
        slac
)

target_compile_features(evse_slac_reset_chip_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_reset_chip_test COMMAND evse_slac_reset_chip_test)

add_executable(evse_slac_event_tx_test evse_slac_event_tx_test.cpp)

target_link_libraries(evse_slac_event_tx_test
    PRIVATE
        slac
)

target_compile_features(evse_slac_event_tx_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_event_tx_test COMMAND evse_slac_event_tx_test)

add_executable(evse_slac_telemetry_test evse_slac_telemetry_test.cpp)

target_link_libraries(evse_slac_telemetry_test
    PRIVATE
        slac
        nlohmann_json::nlohmann_json
)

target_compile_features(evse_slac_telemetry_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_telemetry_test COMMAND evse_slac_telemetry_test)

add_executable(evse_slac_matching_test evse_slac_matching_test.cpp)

target_link_libraries(evse_slac_matching_test
    PRIVATE
        slac
        nlohmann_json::nlohmann_json
)

target_compile_features(evse_slac_matching_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_matching_test COMMAND evse_slac_matching_test)
add_executable(evse_slac_config_test evse_slac_config_test.cpp)

target_link_libraries(evse_slac_config_test
    PRIVATE
        slac
)

target_compile_features(evse_slac_config_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_config_test COMMAND evse_slac_config_test)
add_executable(evse_slac_message_init_test evse_slac_message_init_test.cpp)

target_link_libraries(evse_slac_message_init_test
    PRIVATE
        slac
)

target_compile_features(evse_slac_message_init_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_message_init_test COMMAND evse_slac_message_init_test)
add_executable(evse_slac_timer_test evse_slac_timer_test.cpp)

target_link_libraries(evse_slac_timer_test
    PRIVATE
        slac
)

target_compile_features(evse_slac_timer_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_timer_test COMMAND evse_slac_timer_test)

add_executable(evse_slac_misc_test evse_slac_misc_test.cpp)

target_link_libraries(evse_slac_misc_test
    PRIVATE
        slac
)

target_compile_features(evse_slac_misc_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_misc_test COMMAND evse_slac_misc_test)

add_executable(evse_slac_validate_test evse_slac_validate_test.cpp)

target_link_libraries(evse_slac_validate_test
    PRIVATE
        slac
)

target_compile_features(evse_slac_validate_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_validate_test COMMAND evse_slac_validate_test)

add_executable(evse_slac_amp_map_test evse_slac_amp_map_test.cpp)

target_link_libraries(evse_slac_amp_map_test
    PRIVATE
        slac
)

target_compile_features(evse_slac_amp_map_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_amp_map_test COMMAND evse_slac_amp_map_test)

add_executable(ev_slac_matching_test
    ev_slac_matching_test.cpp
)

target_link_libraries(ev_slac_matching_test
    PRIVATE
        slac
)

target_compile_features(ev_slac_matching_test PRIVATE cxx_std_17)

add_test(NAME ev_slac_matching_test COMMAND ev_slac_matching_test)
