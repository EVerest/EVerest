# ISO 15118-2 EV state tests
include(Catch)

# Select one source file's tests with: test_ev_d2 -# "[#<file stem>]"
add_executable(test_ev_d2
    authorization.cpp
    cable_check.cpp
    charge_parameter_discovery.cpp
    charging_status.cpp
    current_demand.cpp
    metering_receipt.cpp
    payment_details.cpp
    payment_service_selection.cpp
    pnc_material.cpp
    power_delivery.cpp
    pre_charge.cpp
    send_on_transition.cpp
    service_detail.cpp
    service_discovery.cpp
    welding_detection.cpp
    helper.cpp
)
target_link_libraries(test_ev_d2
    PRIVATE
        iso15118
        Catch2::Catch2WithMain
        OpenSSL::Crypto
        everest::tls
)
catch_discover_tests(test_ev_d2 DISCOVERY_MODE PRE_TEST)
