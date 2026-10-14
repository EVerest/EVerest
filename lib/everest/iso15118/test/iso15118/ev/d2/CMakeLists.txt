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
            everest::tls
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
create_ev_d2_test_target(certificate_installation_interop)

# The capture lives under tests/ so the OCPP suite, whose CI image copies only tests/, checks it too.
# Rooted at this library's place in the EVerest tree, which holds for a standalone configure as well.
get_filename_component(EVEREST_REPOSITORY_ROOT "${iso15118_SOURCE_DIR}/../../.." ABSOLUTE)
target_compile_definitions(test_ev_d2_certificate_installation_interop
    PRIVATE
        CERTIFICATE_INSTALLATION_RES_B64="${EVEREST_REPOSITORY_ROOT}/tests/ocpp_tests/test_sets/everest-aux/exi/certificate_installation_res.base64"
)
