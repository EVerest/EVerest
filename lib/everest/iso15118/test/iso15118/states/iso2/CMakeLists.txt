include(Catch)

function(create_states_iso2_test_target NAME)
    add_executable(test_iso2_${NAME} ${NAME}.cpp)
    target_link_libraries(test_iso2_${NAME}
        PRIVATE
            iso15118
            Catch2::Catch2WithMain
    )
    catch_discover_tests(test_iso2_${NAME})
endfunction()

create_states_iso2_test_target(session_setup)
create_states_iso2_test_target(service_discovery)
create_states_iso2_test_target(service_detail)
create_states_iso2_test_target(payment_service_selection)
create_states_iso2_test_target(authorization)
create_states_iso2_test_target(charge_parameter_discovery)
create_states_iso2_test_target(power_delivery)
create_states_iso2_test_target(cable_check)
create_states_iso2_test_target(pre_charge)
create_states_iso2_test_target(current_demand)
create_states_iso2_test_target(charging_status)
create_states_iso2_test_target(welding_detection)
create_states_iso2_test_target(session_stop)
