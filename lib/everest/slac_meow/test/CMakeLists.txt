#
# Tests for the SLAC protocol layer, the transport and the two state machines.
#
# evse_matching_test, evse_set_key_test and ev_matching_test are slac_neo's own suites, carried over unchanged apart
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

# Covers the transport in io/, which the state machines never reach.
add_executable(slac_meow_io_event_test io_event_test.cpp)
target_link_libraries(slac_meow_io_event_test PRIVATE slac_meow)
target_compile_features(slac_meow_io_event_test PRIVATE cxx_std_17)
add_test(NAME slac_meow_io_event_test COMMAND slac_meow_io_event_test)

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

add_executable(ev_slac_meow_matching_test ev_matching_test.cpp)
target_link_libraries(ev_slac_meow_matching_test PRIVATE slac_meow)
target_compile_features(ev_slac_meow_matching_test PRIVATE cxx_std_17)
add_test(NAME ev_slac_meow_matching_test COMMAND ev_slac_meow_matching_test)

# Covers the pure helpers of both sides, which need no state machine to exercise.
add_executable(slac_meow_detail_test detail_test.cpp)
target_link_libraries(slac_meow_detail_test PRIVATE slac_meow)
target_compile_features(slac_meow_detail_test PRIVATE cxx_std_17)
add_test(NAME slac_meow_detail_test COMMAND slac_meow_detail_test)

#
# The two machines run against each other. Catch2, following the style of the iso15118 and
# ieee2030_1_1 FSM tests; the suites carried over from slac_neo stay as they are, since their value
# depends on remaining diffable against the originals.
#
# Catch2 is already provided by the top level build; this only puts its CMake helpers on the
# module path, the same way the iso15118 and ieee2030_1_1 test trees do.
include(FetchContent)
FetchContent_GetProperties(Catch2)
if(catch2_POPULATED)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
include(Catch)

# Each scenario is one .cpp plus the shared harness in virtual_plc_link.cpp, which stands in for
# the PLC medium and for each station's modem. Naming the harness source once here means a second
# scenario is a single line, the way iso15118's create_fsm_test_target does it.
#
# TEST_PREFIX so a scenario is found by `ctest -R slac_meow` alongside the other binaries; without
# it Catch2 registers the test under its scenario name alone.
function(create_slac_meow_scenario_test NAME)
    add_executable(slac_meow_${NAME} ${NAME}.cpp)

    target_sources(slac_meow_${NAME}
        PRIVATE
            virtual_plc_link.cpp
    )

    target_link_libraries(slac_meow_${NAME}
        PRIVATE
            slac_meow
            Catch2::Catch2WithMain
    )

    target_compile_features(slac_meow_${NAME} PRIVATE cxx_std_17)

    catch_discover_tests(slac_meow_${NAME} TEST_PREFIX "slac_meow_${NAME}: ")
endfunction()

create_slac_meow_scenario_test(evse_vs_ev_test)
