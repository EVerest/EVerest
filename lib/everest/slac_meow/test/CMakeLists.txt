#
# Tests for the SLAC protocol layer, the transport and the EVSE state machine.
#
# evse_matching_test and evse_set_key_test are slac_neo's own suites, carried over unchanged apart
# from the include and a using directive, so the two implementations are held to exactly the same
# behaviour and stay diffable against their originals in lib/everest/slac_neo/test.
#
# File names do not repeat slac_meow - the directory already says it. Target names do keep the
# prefix, since they and the ctest names they produce live in a namespace shared with every other
# library's tests, where a bare config_test would be ambiguous.
#
add_executable(slac_meow_message_init_test message_init_test.cpp)
target_link_libraries(slac_meow_message_init_test PRIVATE slac_meow)
target_compile_features(slac_meow_message_init_test PRIVATE cxx_std_17)
add_test(NAME slac_meow_message_init_test COMMAND slac_meow_message_init_test)

add_executable(slac_meow_status_test status_test.cpp)
target_link_libraries(slac_meow_status_test PRIVATE slac_meow nlohmann_json::nlohmann_json)
target_compile_features(slac_meow_status_test PRIVATE cxx_std_17)
add_test(NAME slac_meow_status_test COMMAND slac_meow_status_test)

add_executable(evse_slac_meow_set_key_test
    evse_set_key_test.cpp
    fsm_type_traits_test.cpp
)

target_link_libraries(evse_slac_meow_set_key_test PRIVATE slac_meow)
target_compile_features(evse_slac_meow_set_key_test PRIVATE cxx_std_17)
add_test(NAME evse_slac_meow_set_key_test COMMAND evse_slac_meow_set_key_test)

add_executable(evse_slac_meow_matching_test evse_matching_test.cpp)
target_link_libraries(evse_slac_meow_matching_test PRIVATE slac_meow)
target_compile_features(evse_slac_meow_matching_test PRIVATE cxx_std_17)
add_test(NAME evse_slac_meow_matching_test COMMAND evse_slac_meow_matching_test)

add_executable(slac_meow_config_test config_test.cpp)
target_link_libraries(slac_meow_config_test PRIVATE slac_meow)
target_compile_features(slac_meow_config_test PRIVATE cxx_std_17)
add_test(NAME slac_meow_config_test COMMAND slac_meow_config_test)
