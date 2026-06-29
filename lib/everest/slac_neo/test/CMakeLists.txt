add_executable(evse_slac_neo_set_key_test
    evse_slac_neo_set_key_test.cpp
    slac_fsm_type_traits_test.cpp
)

target_link_libraries(evse_slac_neo_set_key_test
    PRIVATE
        slac_neo
        nlohmann_json::nlohmann_json
)

target_compile_features(evse_slac_neo_set_key_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_neo_set_key_test COMMAND evse_slac_neo_set_key_test)

add_executable(evse_slac_neo_telemetry_test evse_slac_neo_telemetry_test.cpp)

target_link_libraries(evse_slac_neo_telemetry_test
    PRIVATE
        slac_neo
        nlohmann_json::nlohmann_json
)

target_compile_features(evse_slac_neo_telemetry_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_neo_telemetry_test COMMAND evse_slac_neo_telemetry_test)

add_executable(evse_slac_neo_matching_test evse_slac_neo_matching_test.cpp)

target_link_libraries(evse_slac_neo_matching_test
    PRIVATE
        slac_neo
        nlohmann_json::nlohmann_json
)

target_compile_features(evse_slac_neo_matching_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_neo_matching_test COMMAND evse_slac_neo_matching_test)
add_executable(evse_slac_neo_config_test evse_slac_neo_config_test.cpp)

target_link_libraries(evse_slac_neo_config_test
    PRIVATE
        slac_neo
)

target_compile_features(evse_slac_neo_config_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_neo_config_test COMMAND evse_slac_neo_config_test)
add_executable(evse_slac_neo_message_init_test evse_slac_neo_message_init_test.cpp)

target_link_libraries(evse_slac_neo_message_init_test
    PRIVATE
        slac_neo
)

target_compile_features(evse_slac_neo_message_init_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_neo_message_init_test COMMAND evse_slac_neo_message_init_test)
add_executable(evse_slac_neo_timer_test evse_slac_neo_timer_test.cpp)

target_link_libraries(evse_slac_neo_timer_test
    PRIVATE
        slac_neo
)

target_compile_features(evse_slac_neo_timer_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_neo_timer_test COMMAND evse_slac_neo_timer_test)

add_executable(evse_slac_neo_misc_test evse_slac_neo_misc_test.cpp)

target_link_libraries(evse_slac_neo_misc_test
    PRIVATE
        slac_neo
)

target_compile_features(evse_slac_neo_misc_test PRIVATE cxx_std_17)

add_test(NAME evse_slac_neo_misc_test COMMAND evse_slac_neo_misc_test)

add_executable(ev_slac_neo_matching_test
    ev_slac_neo_matching_test.cpp
)

target_link_libraries(ev_slac_neo_matching_test
    PRIVATE
        slac_neo
)

target_compile_features(ev_slac_neo_matching_test PRIVATE cxx_std_17)

add_test(NAME ev_slac_neo_matching_test COMMAND ev_slac_neo_matching_test)
