# DIN SPEC 70121 EV state tests
include(Catch)

# Select one source file's tests with: test_ev_din -# "[#<file stem>]"
add_executable(test_ev_din
    cable_check.cpp
    charge_parameter_discovery.cpp
    contract_authentication.cpp
    current_demand.cpp
    power_delivery.cpp
    pre_charge.cpp
    send_on_transition.cpp
    service_discovery.cpp
    service_payment_selection.cpp
    session_setup.cpp
    session_stop.cpp
    timeouts.cpp
    welding_detection.cpp
    helper.cpp
)
target_link_libraries(test_ev_din
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
)
catch_discover_tests(test_ev_din DISCOVERY_MODE PRE_TEST)
