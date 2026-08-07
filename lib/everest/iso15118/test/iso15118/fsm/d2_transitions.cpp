// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// FSM-level tests of the ISO 15118-2 SECC state machine: every state is entered for real and the
// transition it takes is asserted, which the response-builder tests in states/iso2/ cannot cover. The
// counterpart of d20_transitions.cpp for the -20 state machine.
#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "secc_helper.hpp"

#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/cable_check.hpp>
#include <iso15118/message_2/certificate_installation.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/charging_status.hpp>
#include <iso15118/message_2/current_demand.hpp>
#include <iso15118/message_2/metering_receipt.hpp>
#include <iso15118/message_2/payment_details.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/power_delivery.hpp>
#include <iso15118/message_2/pre_charge.hpp>
#include <iso15118/message_2/service_detail.hpp>
#include <iso15118/message_2/service_discovery.hpp>
#include <iso15118/message_2/session_setup.hpp>
#include <iso15118/message_2/session_stop.hpp>
#include <iso15118/message_2/welding_detection.hpp>

using namespace iso15118;
using iso15118::test::D2SeccFsm;

namespace dt = message_2::datatypes;
using StateID = d2::StateID;

namespace {

constexpr uint16_t CHARGE_SERVICE_ID = 1;
constexpr uint8_t SA_SCHEDULE_TUPLE_ID = 1;

d2::SessionConfig make_config(dt::EnergyTransferMode mode) {
    d2::SessionConfig config;
    config.charge_service_id = CHARGE_SERVICE_ID;
    config.supported_energy_transfer_modes.push_back(mode);
    return config;
}

// Plug-and-Charge setup. tls_active is a plain config flag (the engine derives it from
// IConnection::is_secure()), so the Contract paths need no TLS here -- only the crypto that validates a
// contract chain or a signature needs real certificates, which is why the accepted-chain and
// signed-receipt paths are not covered below.
d2::SessionConfig make_pnc_config() {
    auto config = make_config(dt::EnergyTransferMode::DC_extended);
    config.pnc_enabled = true;
    config.tls_active = true;
    config.cert_install_service = true;
    return config;
}

dt::PhysicalValue volts(double value) {
    return dt::to_physical_value(value, dt::Unit::V);
}

dt::PhysicalValue amps(double value) {
    return dt::to_physical_value(value, dt::Unit::A);
}

// The state machine under test plus the session id the SECC assigned, stamped into every following
// request the way a real EVCC echoes it back.
class Secc {
public:
    explicit Secc(dt::EnergyTransferMode mode = dt::EnergyTransferMode::DC_extended) : fsm(make_config(mode)) {
    }

    Secc(d2::SessionConfig config, session::feedback::Callbacks callbacks) :
        fsm(std::move(config), std::move(callbacks)) {
    }

    template <typename Request> void drive(Request request) {
        request.header.session_id = session_id;
        fsm.drive(request);
    }

    // SessionSetupReq is the only request that carries no assigned session id yet.
    void drive_session_setup() {
        message_2::SessionSetupRequest req;
        req.evcc_id = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
        fsm.drive(req);
        session_id = fsm.context().get_session_id();
    }

    D2SeccFsm fsm;
    dt::SessionId session_id{};
};

message_2::PaymentServiceSelectionRequest payment_selection_req() {
    message_2::PaymentServiceSelectionRequest req;
    req.selected_payment_option = dt::PaymentOption::ExternalPayment;
    req.selected_service_list.push_back({CHARGE_SERVICE_ID, std::nullopt});
    return req;
}

message_2::ChargeParameterDiscoveryRequest charge_parameter_req(dt::EnergyTransferMode mode) {
    message_2::ChargeParameterDiscoveryRequest req;
    req.requested_energy_transfer_mode = mode;
    if (mode == dt::EnergyTransferMode::DC_extended) {
        auto& params = req.dc_ev_charge_parameter.emplace();
        params.ev_maximum_current_limit = amps(200.0);
        params.ev_maximum_voltage_limit = volts(400.0);
    } else {
        auto& params = req.ac_ev_charge_parameter.emplace();
        params.ev_max_voltage = volts(230.0);
        params.ev_max_current = amps(32.0);
        params.ev_min_current = amps(6.0);
    }
    return req;
}

message_2::PreChargeRequest pre_charge_req() {
    message_2::PreChargeRequest req;
    req.ev_target_voltage = volts(400.0);
    req.ev_target_current = amps(2.0);
    return req;
}

// \p max_power_w must stay within the PMax the SECC advertised in its SAScheduleList, otherwise the
// request is answered with FAILED_ChargingProfileInvalid [V2G2-224/225].
message_2::PowerDeliveryRequest power_delivery_req(dt::ChargeProgress progress, double max_power_w = 11000.0) {
    message_2::PowerDeliveryRequest req;
    req.charge_progress = progress;
    req.sa_schedule_tuple_id = SA_SCHEDULE_TUPLE_ID;
    if (progress == dt::ChargeProgress::Start) {
        auto& profile = req.charging_profile.emplace();
        profile.profile_entry.push_back({0, dt::to_physical_value(max_power_w, dt::Unit::W), std::nullopt});
    }
    return req;
}

constexpr double AC_PROFILE_POWER_W = 1000.0;

// The mandatory elements have to be present, otherwise the request does not encode.
message_2::CertificateInstallationRequest certificate_installation_req() {
    message_2::CertificateInstallationRequest req;
    req.oem_provisioning_cert = {0x30, 0x82, 0x01, 0x02};
    req.root_certificate_ids.push_back({"CN=V2G Root CA", 12345});
    return req;
}

message_2::CurrentDemandRequest current_demand_req() {
    message_2::CurrentDemandRequest req;
    req.ev_target_voltage = volts(400.0);
    req.ev_target_current = amps(100.0);
    req.charging_complete = false;
    return req;
}

// --- advance helpers: drive the session up to (and not into) the state under test ---

void to_service_discovery(Secc& secc) {
    secc.drive_session_setup();
}

void to_service_detail(Secc& secc) {
    to_service_discovery(secc);
    secc.drive(message_2::ServiceDiscoveryRequest{});
}

void to_authorization(Secc& secc) {
    to_service_detail(secc);
    // ServiceDetail is optional: a PaymentServiceSelectionReq is deferred to PaymentServiceSelection,
    // which answers it and moves on to Authorization.
    secc.drive(payment_selection_req());
}

void to_charge_parameter_discovery(Secc& secc) {
    to_authorization(secc);
    secc.drive(message_2::AuthorizationRequest{});
    secc.fsm.control(d20::AuthorizationResponse{true});
    secc.drive(message_2::AuthorizationRequest{});
}

void to_cable_check(Secc& secc) {
    to_charge_parameter_discovery(secc);
    secc.drive(charge_parameter_req(dt::EnergyTransferMode::DC_extended));
    secc.fsm.context().current_cp_state = d20::CpState::C;
}

void to_pre_charge(Secc& secc) {
    to_cable_check(secc);
    secc.drive(message_2::CableCheckRequest{});
    secc.fsm.control(d20::CableCheckFinished{true});
    secc.drive(message_2::CableCheckRequest{});
}

void to_current_demand(Secc& secc) {
    to_pre_charge(secc);
    secc.drive(pre_charge_req());
    secc.drive(power_delivery_req(dt::ChargeProgress::Start));
}

void to_welding_detection(Secc& secc) {
    to_current_demand(secc);
    secc.drive(current_demand_req());
    secc.drive(power_delivery_req(dt::ChargeProgress::Stop));
    // [V2G2-920]..[V2G2-922]: after PowerDelivery(Stop) the EV signals CP State B again; without it the
    // WeldingDetectionReq is parked until the CP-state timeout.
    secc.fsm.context().current_cp_state = d20::CpState::B;
}

} // namespace

SCENARIO("ISO 15118-2 SECC handshake state transitions") {

    Secc secc;

    GIVEN("A fresh session machine") {
        REQUIRE(secc.fsm.state() == StateID::SessionSetup);

        WHEN("The EV sends SessionSetupReq") {
            secc.drive_session_setup();

            THEN("The session is established and the machine moves to ServiceDiscovery") {
                REQUIRE(secc.fsm.state() == StateID::ServiceDiscovery);
                const auto res = secc.fsm.response<message_2::SessionSetupResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK_NewSessionEstablished);
                REQUIRE(secc.session_id != dt::SessionId{});
            }
        }

        WHEN("The EV skips SessionSetup and sends a charge loop request") {
            secc.fsm.drive(message_2::CurrentDemandRequest{});

            THEN("The session is sequence-errored and stays put [V2G2-538]") {
                REQUIRE(secc.fsm.state() == StateID::SessionSetup);
                const auto res = secc.fsm.response<message_2::CurrentDemandResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }
    }

    GIVEN("A machine in ServiceDiscovery") {
        to_service_discovery(secc);
        REQUIRE(secc.fsm.state() == StateID::ServiceDiscovery);

        WHEN("The EV sends ServiceDiscoveryReq") {
            secc.drive(message_2::ServiceDiscoveryRequest{});

            THEN("The charge service is offered and the machine moves to ServiceDetail") {
                REQUIRE(secc.fsm.state() == StateID::ServiceDetail);
                const auto res = secc.fsm.response<message_2::ServiceDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(res->charge_service.service_id == CHARGE_SERVICE_ID);
            }
        }
    }

    GIVEN("A machine in ServiceDetail") {
        to_service_detail(secc);
        REQUIRE(secc.fsm.state() == StateID::ServiceDetail);

        WHEN("The EV skips the optional ServiceDetailReq and selects a payment option") {
            secc.drive(payment_selection_req());

            THEN("ServiceDetail defers to PaymentServiceSelection, which answers and moves to Authorization") {
                REQUIRE(secc.fsm.state() == StateID::Authorization);
                const auto res = secc.fsm.response<message_2::PaymentServiceSelectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }
    }

    GIVEN("A machine in Authorization (EIM)") {
        to_authorization(secc);
        REQUIRE(secc.fsm.state() == StateID::Authorization);

        WHEN("Authorization has not been granted yet") {
            secc.drive(message_2::AuthorizationRequest{});

            THEN("The EV is kept polling and the machine stays in Authorization") {
                REQUIRE(secc.fsm.state() == StateID::Authorization);
                const auto res = secc.fsm.response<message_2::AuthorizationResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                // [V2G2-854]: EIM (ExternalPayment) waits with Ongoing_WaitingForCustomerInteraction;
                // plain Ongoing is the PnC (Contract) case [V2G2-855].
                REQUIRE(res->evse_processing == dt::EVSEProcessing::Ongoing_WaitingForCustomerInteraction);
            }
        }

        WHEN("The module authorizes the session and the EV polls again") {
            secc.drive(message_2::AuthorizationRequest{});
            secc.fsm.control(d20::AuthorizationResponse{true});
            secc.drive(message_2::AuthorizationRequest{});

            THEN("Processing finishes and the machine moves to ChargeParameterDiscovery") {
                REQUIRE(secc.fsm.state() == StateID::ChargeParameterDiscovery);
                const auto res = secc.fsm.response<message_2::AuthorizationResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->evse_processing == dt::EVSEProcessing::Finished);
            }
        }

        WHEN("The EV aborts with SessionStopReq") {
            secc.drive(message_2::SessionStopRequest{});

            THEN("The machine hands over to SessionStop and answers it") {
                REQUIRE(secc.fsm.state() == StateID::SessionStop);
                const auto res = secc.fsm.response<message_2::SessionStopResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }
    }
}

SCENARIO("ISO 15118-2 SECC DC charging state transitions") {

    Secc secc;

    GIVEN("A machine in ChargeParameterDiscovery") {
        to_charge_parameter_discovery(secc);
        REQUIRE(secc.fsm.state() == StateID::ChargeParameterDiscovery);

        WHEN("The EV asks for DC_extended charge parameters") {
            secc.drive(charge_parameter_req(dt::EnergyTransferMode::DC_extended));

            THEN("The machine moves to CableCheck") {
                REQUIRE(secc.fsm.state() == StateID::CableCheck);
                const auto res = secc.fsm.response<message_2::ChargeParameterDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(res->evse_processing == dt::EVSEProcessing::Finished);
                REQUIRE(secc.fsm.context().dc_charging);
            }
        }
    }

    GIVEN("A machine in CableCheck with the EV in CP State C") {
        to_cable_check(secc);
        REQUIRE(secc.fsm.state() == StateID::CableCheck);

        WHEN("The cable check has not finished yet") {
            secc.drive(message_2::CableCheckRequest{});

            THEN("The EV is kept polling and the machine stays in CableCheck") {
                REQUIRE(secc.fsm.state() == StateID::CableCheck);
                const auto res = secc.fsm.response<message_2::CableCheckResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->evse_processing == dt::EVSEProcessing::Ongoing);
            }
        }

        WHEN("The module reports the cable check finished") {
            secc.drive(message_2::CableCheckRequest{});
            secc.fsm.control(d20::CableCheckFinished{true});
            secc.drive(message_2::CableCheckRequest{});

            THEN("The machine moves to PreCharge") {
                REQUIRE(secc.fsm.state() == StateID::PreCharge);
                const auto res = secc.fsm.response<message_2::CableCheckResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->evse_processing == dt::EVSEProcessing::Finished);
            }
        }
    }

    GIVEN("A machine in PreCharge") {
        to_pre_charge(secc);
        REQUIRE(secc.fsm.state() == StateID::PreCharge);

        WHEN("The EV pre-charges") {
            secc.drive(pre_charge_req());

            THEN("The machine stays in PreCharge") {
                REQUIRE(secc.fsm.state() == StateID::PreCharge);
                const auto res = secc.fsm.response<message_2::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }

        WHEN("The EV ends pre-charge with PowerDeliveryReq(Start)") {
            secc.drive(pre_charge_req());
            secc.drive(power_delivery_req(dt::ChargeProgress::Start));

            THEN("PreCharge defers to PowerDelivery, which answers and starts the DC charge loop") {
                REQUIRE(secc.fsm.state() == StateID::CurrentDemand);
                const auto res = secc.fsm.response<message_2::PowerDeliveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }
    }

    GIVEN("A machine in the CurrentDemand charge loop") {
        to_current_demand(secc);
        REQUIRE(secc.fsm.state() == StateID::CurrentDemand);

        WHEN("The EV keeps demanding current") {
            secc.drive(current_demand_req());

            THEN("The machine stays in CurrentDemand") {
                REQUIRE(secc.fsm.state() == StateID::CurrentDemand);
                const auto res = secc.fsm.response<message_2::CurrentDemandResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }

        WHEN("The EV sends a MeteringReceipt that was never requested") {
            secc.drive(current_demand_req());
            message_2::MeteringReceiptRequest req;
            req.session_id = secc.session_id;
            secc.drive(req);

            THEN("The charge loop hands over to MeteringReceipt, which sequence-errors it [V2G2-691]") {
                REQUIRE(secc.fsm.state() == StateID::MeteringReceipt);
                const auto res = secc.fsm.response<message_2::MeteringReceiptResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }

        WHEN("The EV sends a MeteringReceipt bound to a different session") {
            secc.drive(current_demand_req());
            message_2::MeteringReceiptRequest req;
            req.session_id = dt::SessionId{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
            secc.drive(req);

            THEN("The receipt is rejected with FAILED_UnknownSession [V2G2-909]") {
                REQUIRE(secc.fsm.state() == StateID::MeteringReceipt);
                const auto res = secc.fsm.response<message_2::MeteringReceiptResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_UnknownSession);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }

        WHEN("The EV stops charging with PowerDeliveryReq(Stop)") {
            secc.drive(current_demand_req());
            secc.drive(power_delivery_req(dt::ChargeProgress::Stop));

            THEN("The charge loop defers to PowerDelivery and the machine moves to WeldingDetection") {
                REQUIRE(secc.fsm.state() == StateID::WeldingDetection);
                const auto res = secc.fsm.response<message_2::PowerDeliveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }
    }

    GIVEN("A machine in WeldingDetection with the EV back in CP State B") {
        to_welding_detection(secc);
        REQUIRE(secc.fsm.state() == StateID::WeldingDetection);

        WHEN("The EV runs welding detection") {
            secc.drive(message_2::WeldingDetectionRequest{});

            THEN("The machine stays in WeldingDetection") {
                REQUIRE(secc.fsm.state() == StateID::WeldingDetection);
                const auto res = secc.fsm.response<message_2::WeldingDetectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }

        WHEN("The EV ends the session") {
            secc.drive(message_2::WeldingDetectionRequest{});
            secc.drive(message_2::SessionStopRequest{});

            THEN("WeldingDetection defers to SessionStop, which answers and ends the session") {
                REQUIRE(secc.fsm.state() == StateID::SessionStop);
                const auto res = secc.fsm.response<message_2::SessionStopResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }
    }
}

SCENARIO("ISO 15118-2 SECC AC charging state transitions") {

    Secc secc{dt::EnergyTransferMode::AC_three_phase_core};

    GIVEN("An AC session in ChargeParameterDiscovery") {
        to_charge_parameter_discovery(secc);
        REQUIRE(secc.fsm.state() == StateID::ChargeParameterDiscovery);

        WHEN("The EV asks for AC charge parameters") {
            secc.drive(charge_parameter_req(dt::EnergyTransferMode::AC_three_phase_core));

            THEN("AC skips the cable check and the machine moves to PowerDelivery") {
                REQUIRE(secc.fsm.state() == StateID::PowerDelivery);
                const auto res = secc.fsm.response<message_2::ChargeParameterDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE_FALSE(secc.fsm.context().dc_charging);
            }
        }

        WHEN("The EV starts charging before the contactor is closed") {
            secc.drive(charge_parameter_req(dt::EnergyTransferMode::AC_three_phase_core));
            secc.drive(power_delivery_req(dt::ChargeProgress::Start, AC_PROFILE_POWER_W));

            THEN("The response is held back until the module reports the contactor closed") {
                REQUIRE(secc.fsm.state() == StateID::PowerDelivery);
                REQUIRE_FALSE(secc.fsm.has_response());
            }

            AND_WHEN("The contactor closes") {
                secc.fsm.control(d20::ClosedContactor{true});

                THEN("The held PowerDeliveryRes is sent and the machine moves to ChargingStatus") {
                    REQUIRE(secc.fsm.state() == StateID::ChargingStatus);
                    const auto res = secc.fsm.response<message_2::PowerDeliveryResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::OK);
                }
            }
        }
    }
}

SCENARIO("ISO 15118-2 SECC Plug-and-Charge state transitions") {

    // What the module saw of the relayed certificate exchange.
    std::optional<std::string> forwarded_exi_request;
    session::feedback::Callbacks callbacks;
    callbacks.certificate_request = [&forwarded_exi_request](const std::string& exi_request_base64,
                                                             session::feedback::CertificateExchangeAction) {
        forwarded_exi_request = exi_request_base64;
    };

    Secc secc{make_pnc_config(), callbacks};

    // Contract payment, with the certificate service selected for installation (parameter set 1).
    const auto contract_selection_req = [] {
        message_2::PaymentServiceSelectionRequest req;
        req.selected_payment_option = dt::PaymentOption::Contract;
        req.selected_service_list.push_back({CHARGE_SERVICE_ID, std::nullopt});
        req.selected_service_list.push_back({dt::CERTIFICATE_SERVICE_ID, 1});
        return req;
    };

    const auto to_payment_details = [&](bool select_certificate_service) {
        secc.drive_session_setup();
        secc.drive(message_2::ServiceDiscoveryRequest{});
        auto req = contract_selection_req();
        if (not select_certificate_service) {
            req.selected_service_list.clear();
            req.selected_service_list.push_back({CHARGE_SERVICE_ID, std::nullopt});
        }
        secc.drive(req);
    };

    GIVEN("A machine in PaymentServiceSelection with PnC enabled") {
        secc.drive_session_setup();
        secc.drive(message_2::ServiceDiscoveryRequest{});
        REQUIRE(secc.fsm.state() == StateID::ServiceDetail);

        WHEN("The EV selects Contract payment") {
            secc.drive(contract_selection_req());

            THEN("The machine moves to PaymentDetails instead of Authorization [V2G2-432]") {
                REQUIRE(secc.fsm.state() == StateID::PaymentDetails);
                const auto res = secc.fsm.response<message_2::PaymentServiceSelectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(secc.fsm.context().contract_selected);
                REQUIRE(secc.fsm.context().cert_install_selected);
            }
        }
    }

    GIVEN("A machine in PaymentDetails") {
        to_payment_details(true);
        REQUIRE(secc.fsm.state() == StateID::PaymentDetails);

        WHEN("The EV sends a contract certificate that does not parse") {
            message_2::PaymentDetailsRequest req;
            req.emaid = "DEPNX123456789"; // 14-15 characters, or the EXI document does not decode
            req.contract_certificate = {0x01, 0x02, 0x03};
            secc.drive(req);

            THEN("The chain is rejected and the session ends without a transition") {
                REQUIRE(secc.fsm.state() == StateID::PaymentDetails);
                const auto res = secc.fsm.response<message_2::PaymentDetailsResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code >= dt::ResponseCode::FAILED);
                // GenChallenge is mandatory even on a failed response.
                REQUIRE_FALSE(res->gen_challenge.empty());
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }

        WHEN("The EV runs the selected certificate installation first") {
            secc.drive(certificate_installation_req());

            THEN("The machine moves to CertificateInstallation and forwards the request to the module") {
                REQUIRE(secc.fsm.state() == StateID::CertificateInstallation);
                REQUIRE(forwarded_exi_request.has_value());
                REQUIRE_FALSE(forwarded_exi_request->empty());
                // Nothing is answered until the backend responds.
                REQUIRE_FALSE(secc.fsm.has_response());
            }

            AND_WHEN("The backend returns a CertificateInstallationRes") {
                secc.fsm.control(d20::CertificateResponse{true, "3q2+7w=="}); // arbitrary EXI bytes

                THEN("The raw response is spliced onto the wire and the machine returns to PaymentDetails") {
                    REQUIRE(secc.fsm.state() == StateID::PaymentDetails);
                    REQUIRE(secc.fsm.has_response());
                }
            }

            AND_WHEN("The backend rejects the request") {
                secc.fsm.control(d20::CertificateResponse{false, {}});

                THEN("The session is terminated") {
                    REQUIRE(secc.fsm.context().session_stopped);
                    REQUIRE_FALSE(secc.fsm.has_response());
                }
            }
        }
    }

    GIVEN("A machine in PaymentDetails without the certificate service selected") {
        to_payment_details(false);
        REQUIRE(secc.fsm.state() == StateID::PaymentDetails);

        WHEN("The EV runs a certificate installation anyway") {
            secc.drive(certificate_installation_req());

            THEN("The unselected action is out of sequence [V2G2-539]") {
                REQUIRE(secc.fsm.state() == StateID::PaymentDetails);
                REQUIRE(secc.fsm.context().session_stopped);
                // Not relayed to the module: the EV never selected this action [V2G2-432].
                REQUIRE_FALSE(forwarded_exi_request.has_value());
                // [V2G2-538]: the FAILED_SequenceError still has to reach the EV before the close.
                const auto res = secc.fsm.response<message_2::CertificateInstallationResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
            }
        }
    }
}
