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
create_fsm_test_target(authorization_setup)
create_fsm_test_target(authorization)
create_fsm_test_target(service_discovery)
create_fsm_test_target(service_detail)
create_fsm_test_target(service_selection)
create_fsm_test_target(ac_charge_loop)
create_fsm_test_target(ac_charge_parameter_discovery)
create_fsm_test_target(dc_charge_parameter_discovery)
create_fsm_test_target(dc_cable_check)
create_fsm_test_target(dc_pre_charge)
create_fsm_test_target(dc_charge_loop)
create_fsm_test_target(dc_welding_detection)
create_fsm_test_target(power_delivery)
create_fsm_test_target(schedule_exchange)
create_fsm_test_target(session_stop)

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
