# DIN SPEC 70121 EV state tests
include(Catch)

function(create_ev_din_test_target NAME)
    add_executable(test_ev_din_${NAME} ${NAME}.cpp)
    target_sources(test_ev_din_${NAME}
        PRIVATE
            helper.cpp
    )
    target_link_libraries(test_ev_din_${NAME}
        PRIVATE
            iso15118
            Catch2::Catch2WithMain
    )
    catch_discover_tests(test_ev_din_${NAME})
endfunction()

create_ev_din_test_target(service_discovery)
create_ev_din_test_target(service_payment_selection)
create_ev_din_test_target(contract_authentication)
create_ev_din_test_target(charge_parameter_discovery)
create_ev_din_test_target(cable_check)
create_ev_din_test_target(pre_charge)
create_ev_din_test_target(power_delivery)
create_ev_din_test_target(current_demand)
create_ev_din_test_target(welding_detection)
create_ev_din_test_target(session_stop)
