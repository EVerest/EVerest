include(Catch)

function(create_ev_fsm_test_target NAME)
    add_executable(test_ev_fsm_${NAME} ${NAME}.cpp)
    target_sources(test_ev_fsm_${NAME}
        PRIVATE
            helper.cpp
    )
    target_link_libraries(test_ev_fsm_${NAME}
        PRIVATE
            iso15118
            Catch2::Catch2WithMain
    )
    catch_discover_tests(test_ev_fsm_${NAME})
endfunction()

create_ev_fsm_test_target(context)
create_ev_fsm_test_target(supported_app_protocol)
create_ev_fsm_test_target(session_setup)
create_ev_fsm_test_target(authorization_setup)
create_ev_fsm_test_target(authorization)
create_ev_fsm_test_target(service_discovery)
create_ev_fsm_test_target(service_detail)
create_ev_fsm_test_target(service_selection)
create_ev_fsm_test_target(ac_charge_parameter_discovery)
create_ev_fsm_test_target(ac_charge_loop)
create_ev_fsm_test_target(ac_der_iec_charge_parameter_discovery)
create_ev_fsm_test_target(ac_der_iec_charge_loop)
create_ev_fsm_test_target(dc_charge_parameter_discovery)
create_ev_fsm_test_target(schedule_exchange)
create_ev_fsm_test_target(dc_cable_check)
create_ev_fsm_test_target(dc_pre_charge)
create_ev_fsm_test_target(dc_charge_loop)
create_ev_fsm_test_target(power_delivery)
create_ev_fsm_test_target(session_stop)
create_ev_fsm_test_target(dc_welding_detection)
create_ev_fsm_test_target(message_exchange)
create_ev_fsm_test_target(dc_charge_params)
