include(Catch)

function(create_fsm_test_target NAME)
    add_executable(test_fsm_${NAME} ${NAME}.cpp)
    target_sources(test_fsm_${NAME}
        PRIVATE
            helper.cpp
    )
    target_link_libraries(test_fsm_${NAME}
        PRIVATE
            iso15118
            Catch2::Catch2WithMain
    )
    catch_discover_tests(test_fsm_${NAME})
endfunction()

create_fsm_test_target(session_setup)
create_fsm_test_target(ev_handshake)
create_fsm_test_target(ev_sap_priority)
create_fsm_test_target(ev_dc_charge)
create_fsm_test_target(ev_din_dc_charge)
create_fsm_test_target(ev_ac_charge)
create_fsm_test_target(ev_iso2_dc_charge)
create_fsm_test_target(ev_iso2_ac_charge)
create_fsm_test_target(service_detail)
create_fsm_test_target(dc_pre_charge)
create_fsm_test_target(der_iec_charge_parameter_discovery)
create_fsm_test_target(der_iec_charge_loop)

add_executable(test_d20_transitions d20_transitions.cpp)

target_sources(test_d20_transitions
    PRIVATE
        helper.cpp
)

target_link_libraries(test_d20_transitions
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)

catch_discover_tests(test_d20_transitions)
