# ISO 15118-2 EV state tests
include(Catch)

function(create_ev_d2_test_target NAME)
    add_executable(test_ev_d2_${NAME} ${NAME}.cpp)
    target_sources(test_ev_d2_${NAME}
        PRIVATE
            helper.cpp
    )
    target_link_libraries(test_ev_d2_${NAME}
        PRIVATE
            iso15118
            Catch2::Catch2WithMain
            OpenSSL::Crypto
    )
    catch_discover_tests(test_ev_d2_${NAME})
endfunction()

create_ev_d2_test_target(service_discovery)
create_ev_d2_test_target(payment_service_selection)
create_ev_d2_test_target(payment_details)
create_ev_d2_test_target(authorization)
create_ev_d2_test_target(charge_parameter_discovery)
create_ev_d2_test_target(power_delivery)
create_ev_d2_test_target(cable_check)
create_ev_d2_test_target(pre_charge)
create_ev_d2_test_target(current_demand)
create_ev_d2_test_target(charging_status)
create_ev_d2_test_target(welding_detection)
create_ev_d2_test_target(metering_receipt)
create_ev_d2_test_target(crypto)
