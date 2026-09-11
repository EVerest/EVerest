// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <vector>

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
    // The limits default to 0 (an unreported value is never advertised), which would make PowerDelivery
    // reject the fixture EV's charging profile against a 0 W PMax.
    config.dc_capability_max_power = 150000.0f;
    config.dc_capability_max_current = 300.0f;
    config.dc_capability_max_voltage = 900.0f;
    config.dc_max_power = 150000.0f;
    config.dc_max_current = 300.0f;
    config.dc_max_voltage = 900.0f;
    config.ac_capability_max_current = 32.0f;
    config.ac_max_current = 32.0f;
    return config;
}

// tls_active is a plain config flag, so the Contract paths need no TLS here. Only the crypto that
// validates a chain or a signature needs real certificates, which is why the accepted-chain and
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

// The session id is stamped into every following request the way a real EVCC echoes it back.
class Secc {
public:
    explicit Secc(dt::EnergyTransferMode mode = dt::EnergyTransferMode::DC_extended) :
        fsm(make_config(mode), make_callbacks()) {
    }

    Secc(d2::SessionConfig config, session::feedback::Callbacks callbacks) :
        fsm(std::move(config), std::move(callbacks)) {
    }

    template <typename Request> void drive(Request request) {
        request.header.session_id = session_id;
        fsm.drive(request);
    }

    template <typename Request> void drive_wrong_session(Request request) {
        request.header.session_id = session_id;
        request.header.session_id[0] ^= 0xFF;
        fsm.drive(request);
    }

    // SessionSetupReq is the only request that carries no assigned session id yet.
    void drive_session_setup() {
        message_2::SessionSetupRequest req;
        req.evcc_id = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
        fsm.drive(req);
        session_id = fsm.context().get_session_id();
    }

    bool saw_signal(session::feedback::Signal signal) const {
        return std::find(signals.begin(), signals.end(), signal) != signals.end();
    }

    std::optional<session::feedback::EvChargeParameters> ev_charge_parameters;
    std::optional<session::feedback::DcMaximumLimits> dc_max_limits;
    std::vector<session::feedback::DcEvChargeProgress> charge_progress;
    std::vector<session::feedback::Signal> signals;
    std::optional<shared_datatypes::PaymentOption> selected_payment_option;
    int setpoints_forwarded{0};
    // Reported as StateBase::feed() consumes the request, so this also proves each frame is consumed once.
    std::vector<iso15118::V2gMessageType> v2g_messages;
    D2SeccFsm fsm;
    dt::SessionId session_id{};

private:
    session::feedback::Callbacks make_callbacks() {
        session::feedback::Callbacks callbacks;
        callbacks.signal = [this](session::feedback::Signal signal) { signals.push_back(signal); };
        callbacks.v2g_message = [this](const iso15118::V2gMessageType& type, const iso15118::io::StreamInputView&) {
            v2g_messages.push_back(type);
        };
        callbacks.selected_payment_option = [this](shared_datatypes::PaymentOption option) {
            selected_payment_option = option;
        };
        callbacks.ev_charge_parameters = [this](const session::feedback::EvChargeParameters& parameters) {
            ev_charge_parameters = parameters;
        };
        callbacks.dc_max_limits = [this](const session::feedback::DcMaximumLimits& limits) { dc_max_limits = limits; };
        callbacks.dc_ev_charge_progress = [this](const session::feedback::DcEvChargeProgress& progress) {
            charge_progress.push_back(progress);
        };
        callbacks.dc_charge_loop_req = [this](const session::feedback::DcChargeLoopReq&) { ++setpoints_forwarded; };
        return callbacks;
    }
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

// Must stay within the advertised PMax, or the request is answered FAILED_ChargingProfileInvalid.
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

void to_service_discovery(Secc& secc) {
    secc.drive_session_setup();
}

void to_service_detail(Secc& secc) {
    to_service_discovery(secc);
    secc.drive(message_2::ServiceDiscoveryRequest{});
}

void to_authorization(Secc& secc) {
    to_service_detail(secc);
    // ServiceDetail is optional, so a PaymentServiceSelectionReq is deferred to PaymentServiceSelection.
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
    secc.fsm.context().set_cp_state(d20::CpState::C);
}

void to_pre_charge(Secc& secc) {
    to_cable_check(secc);
    secc.drive(message_2::CableCheckRequest{});
    secc.fsm.control(d20::CableCheckFinished{true});
    secc.drive(message_2::CableCheckRequest{});
}

// One PreChargeReq beyond to_pre_charge(), which lands on PreChargeStart.
void to_pre_charge_loop(Secc& secc) {
    to_pre_charge(secc);
    secc.drive(pre_charge_req());
}

void to_current_demand(Secc& secc) {
    to_pre_charge_loop(secc);
    secc.drive(power_delivery_req(dt::ChargeProgress::Start));
}

// The AC setup lands on AcChargeLoopStart; one ChargingStatusReq moves into the loop proper.
void to_ac_charge_loop(Secc& secc) {
    to_charge_parameter_discovery(secc);
    secc.drive(charge_parameter_req(dt::EnergyTransferMode::AC_three_phase_core));
    secc.drive(power_delivery_req(dt::ChargeProgress::Start, AC_PROFILE_POWER_W));
    secc.fsm.control(d20::ClosedContactor{true});
    secc.drive(message_2::ChargingStatusRequest{});
}

void to_welding_detection(Secc& secc) {
    to_current_demand(secc);
    secc.drive(current_demand_req());
    secc.drive(power_delivery_req(dt::ChargeProgress::Stop));
    // [V2G2-920]..[V2G2-922]: without CP State B the WeldingDetectionReq is parked until the timeout.
    secc.fsm.context().set_cp_state(d20::CpState::B);
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

        // Regression pin: the type used to be reported by the engine ahead of the feed, back when that was
        // the only code holding an unconsumed request. One entry per frame, in order, shows no double feed.
        WHEN("The EV sends the first two requests of the handshake") {
            secc.drive_session_setup();
            secc.drive(message_2::ServiceDiscoveryRequest{});

            THEN("Each request type is reported to the module exactly once, in order") {
                const std::vector<iso15118::V2gMessageType> expected{message_2::Type::SessionSetupReq,
                                                                     message_2::Type::ServiceDiscoveryReq};
                REQUIRE(secc.v2g_messages == expected);
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

            THEN("The charge service is offered and the machine moves to ServiceSelection") {
                REQUIRE(secc.fsm.state() == StateID::ServiceSelection);
                const auto res = secc.fsm.response<message_2::ServiceDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(res->charge_service.service_id == CHARGE_SERVICE_ID);
            }
        }
    }

    GIVEN("A machine in ServiceSelection") {
        to_service_detail(secc);
        REQUIRE(secc.fsm.state() == StateID::ServiceSelection);

        WHEN("The EV skips the optional ServiceDetailReq and selects a payment option") {
            secc.drive(payment_selection_req());

            THEN("ServiceSelection answers it and moves to Authorization") {
                REQUIRE(secc.fsm.state() == StateID::Authorization);
                const auto res = secc.fsm.response<message_2::PaymentServiceSelectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(secc.selected_payment_option == shared_datatypes::PaymentOption::ExternalPayment);
            }
        }

        WHEN("The EV selects a payment option that was never offered") {
            auto req = payment_selection_req();
            req.selected_payment_option = dt::PaymentOption::Contract;
            secc.drive(req);

            THEN("The selection is rejected [V2G2-465] and nothing is reported to the module") {
                const auto res = secc.fsm.response<message_2::PaymentServiceSelectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_PaymentSelectionInvalid);
                REQUIRE_FALSE(secc.selected_payment_option.has_value());
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
                // [V2G2-854]: EIM waits with Ongoing_WaitingForCustomerInteraction; plain Ongoing is PnC [V2G2-855].
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

        WHEN("The EV sends SessionStopReq mid-negotiation") {
            secc.drive(message_2::SessionStopRequest{});

            // Figures 103/104 place SessionStopReq in two nodes only, so [V2G2-538] makes it out of sequence
            // here. The spec's abort path is a connection teardown, not a SessionStopReq.
            THEN("It is out of sequence and Authorization answers it") {
                REQUIRE(secc.fsm.state() == StateID::Authorization);
                const auto res = secc.fsm.response<message_2::SessionStopResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
                REQUIRE(secc.fsm.context().session_stopped);
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

            THEN("The machine moves to PreChargeStart [V2G2-584]") {
                REQUIRE(secc.fsm.state() == StateID::PreChargeStart);
                const auto res = secc.fsm.response<message_2::CableCheckResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->evse_processing == dt::EVSEProcessing::Finished);
            }
        }
    }

    GIVEN("A machine in PreChargeStart") {
        to_pre_charge(secc);
        REQUIRE(secc.fsm.state() == StateID::PreChargeStart);

        WHEN("The EV pre-charges") {
            secc.drive(pre_charge_req());

            THEN("The machine moves to the pre-charge loop [V2G2-587]") {
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
                REQUIRE(secc.fsm.state() == StateID::DcChargeLoop);
                const auto res = secc.fsm.response<message_2::PowerDeliveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }
    }

    GIVEN("A machine in the CurrentDemand charge loop") {
        to_current_demand(secc);
        REQUIRE(secc.fsm.state() == StateID::DcChargeLoop);

        WHEN("The EV keeps demanding current") {
            secc.drive(current_demand_req());

            THEN("The machine stays in CurrentDemand") {
                REQUIRE(secc.fsm.state() == StateID::DcChargeLoop);
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

            THEN("The charge loop answers it with a sequence error and stays [V2G2-691]") {
                REQUIRE(secc.fsm.state() == StateID::DcChargeLoop);
                const auto res = secc.fsm.response<message_2::MeteringReceiptResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }

        // Reaching MeteringReceipt needs ReceiptRequired=TRUE, which is PnC-only and gated on a meter
        // reading [V2G2-902]. The fixture PKI has no contract chain, so the session facts are set directly.
        WHEN("The SECC asks for a signed receipt") {
            secc.fsm.context().session_config.receipt_required = true;
            secc.fsm.context().set_contract_selected();
            dt::MeterInfo meter;
            meter.meter_id = "EVEREST";
            meter.meter_reading = 4242;
            secc.fsm.context().set_meter_info(meter);
            secc.drive(current_demand_req());

            THEN("The response sets ReceiptRequired and the machine waits for the receipt [V2G2-795]") {
                REQUIRE(secc.fsm.state() == StateID::MeteringReceipt);
                const auto res = secc.fsm.response<message_2::CurrentDemandResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->receipt_required.value_or(false));
            }

            AND_WHEN("The EV continues the loop instead of signing") {
                secc.drive(current_demand_req());

                THEN("It is out of sequence: the receipt is the only request allowed now") {
                    const auto res = secc.fsm.response<message_2::CurrentDemandResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
                    REQUIRE(secc.fsm.context().session_stopped);
                }
            }

            AND_WHEN("The EV sends a receipt bound to a different session") {
                message_2::MeteringReceiptRequest req;
                req.session_id = dt::SessionId{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
                secc.drive(req);

                THEN("The receipt is rejected with FAILED_UnknownSession [V2G2-909]") {
                    const auto res = secc.fsm.response<message_2::MeteringReceiptResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::FAILED_UnknownSession);
                    REQUIRE(secc.fsm.context().session_stopped);
                }
            }
        }

        WHEN("The EV stops charging with PowerDeliveryReq(Stop)") {
            secc.drive(current_demand_req());
            secc.drive(power_delivery_req(dt::ChargeProgress::Stop));

            THEN("The charge loop defers to PowerDelivery and the machine moves to PostCharge") {
                REQUIRE(secc.fsm.state() == StateID::PostCharge);
                const auto res = secc.fsm.response<message_2::PowerDeliveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(secc.saw_signal(session::feedback::Signal::CHARGE_LOOP_STARTED));
                REQUIRE(secc.saw_signal(session::feedback::Signal::CHARGE_LOOP_FINISHED));
            }
        }
    }

    GIVEN("A machine in PostCharge with the EV back in CP State B") {
        to_welding_detection(secc);
        REQUIRE(secc.fsm.state() == StateID::PostCharge);

        WHEN("The EV runs welding detection") {
            secc.drive(message_2::WeldingDetectionRequest{});

            THEN("The machine moves to WeldingDetection, where a restart is no longer in sequence") {
                REQUIRE(secc.fsm.state() == StateID::WeldingDetection);
                const auto res = secc.fsm.response<message_2::WeldingDetectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }

        WHEN("The EV ends the session") {
            secc.drive(message_2::WeldingDetectionRequest{});
            secc.drive(message_2::SessionStopRequest{});

            // The terminal is reached from the welding-detection node, so the SECC answers where it stands.
            THEN("WeldingDetection answers it and the session ends") {
                REQUIRE(secc.fsm.state() == StateID::WeldingDetection);
                const auto res = secc.fsm.response<message_2::SessionStopResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }
    }
}

SCENARIO("ISO 15118-2 SECC EV facts reported to the module") {

    GIVEN("A DC machine in ChargeParameterDiscovery") {
        Secc secc;
        to_charge_parameter_discovery(secc);

        WHEN("The EV sends its DC charge parameters") {
            auto req = charge_parameter_req(dt::EnergyTransferMode::DC_extended);
            auto& params = req.dc_ev_charge_parameter.value();
            params.ev_maximum_power_limit = dt::to_physical_value(80000.0, dt::Unit::W);
            params.ev_energy_capacity = dt::to_physical_value(64000.0, dt::Unit::Wh);
            params.ev_energy_request = dt::to_physical_value(32000.0, dt::Unit::Wh);
            params.full_soc = 97;
            params.bulk_soc = 80;
            params.dc_ev_status.ev_ress_soc = 42;
            params.departure_time = 7200;
            secc.drive(req);

            THEN("They are forwarded to the module, requested mode and departure time included") {
                REQUIRE(secc.ev_charge_parameters.has_value());
                REQUIRE(secc.ev_charge_parameters->requested_energy_transfer ==
                        shared_datatypes::EnergyTransferMode::DC_extended);
                REQUIRE(secc.ev_charge_parameters->departure_time.value() == 7200);
                REQUIRE(secc.ev_charge_parameters->dc.has_value());
                const auto& dc = secc.ev_charge_parameters->dc.value();
                REQUIRE(dc.max_current == 200.0f);
                REQUIRE(dc.max_voltage == 400.0f);
                REQUIRE(dc.max_power.value() == 80000.0f);
                REQUIRE(dc.energy_capacity.value() == 64000.0f);
                REQUIRE(dc.energy_request.value() == 32000.0f);
                REQUIRE(dc.full_soc.value() == 97);
                REQUIRE(dc.bulk_soc.value() == 80);
                REQUIRE(dc.ress_soc == 42);
                REQUIRE_FALSE(secc.ev_charge_parameters->ac.has_value());
            }

            THEN("The same maxima reach the power supply") {
                REQUIRE(secc.dc_max_limits.has_value());
                REQUIRE(secc.dc_max_limits->voltage == 400.0f);
                REQUIRE(secc.dc_max_limits->current == 200.0f);
                REQUIRE(secc.dc_max_limits->power.value() == 80000.0f);
            }
        }

        WHEN("The EV omits the optional EVMaximumPowerLimit") {
            secc.drive(charge_parameter_req(dt::EnergyTransferMode::DC_extended));

            THEN("No power limit is reported, rather than a voltage * current product") {
                REQUIRE(secc.dc_max_limits.has_value());
                REQUIRE(secc.dc_max_limits->voltage == 400.0f);
                REQUIRE(secc.dc_max_limits->current == 200.0f);
                REQUIRE_FALSE(secc.dc_max_limits->power.has_value());

                REQUIRE(secc.ev_charge_parameters.has_value());
                REQUIRE(secc.ev_charge_parameters->dc.has_value());
                REQUIRE_FALSE(secc.ev_charge_parameters->dc->max_power.has_value());
            }
        }
    }

    GIVEN("An AC machine in ChargeParameterDiscovery") {
        Secc secc{dt::EnergyTransferMode::AC_three_phase_core};
        to_charge_parameter_discovery(secc);

        WHEN("The EV sends its AC charge parameters") {
            auto req = charge_parameter_req(dt::EnergyTransferMode::AC_three_phase_core);
            req.ac_ev_charge_parameter->e_amount = dt::to_physical_value(20000.0, dt::Unit::Wh);
            secc.drive(req);

            THEN("The AC limits and the requested energy amount reach the module") {
                REQUIRE(secc.ev_charge_parameters.has_value());
                REQUIRE(secc.ev_charge_parameters->requested_energy_transfer ==
                        shared_datatypes::EnergyTransferMode::AC_three_phase_core);
                REQUIRE(secc.ev_charge_parameters->ac.has_value());
                const auto& ac = secc.ev_charge_parameters->ac.value();
                REQUIRE(ac.e_amount == 20000.0f);
                REQUIRE(ac.max_voltage == 230.0f);
                REQUIRE(ac.max_current == 32.0f);
                REQUIRE(ac.min_current == 6.0f);
                REQUIRE_FALSE(secc.ev_charge_parameters->dc.has_value());
            }
        }
    }

    GIVEN("A machine in CurrentDemand") {
        Secc secc;
        to_current_demand(secc);
        secc.charge_progress.clear();

        WHEN("The EV reports its remaining charging times") {
            auto req = current_demand_req();
            req.remaining_time_to_full_soc = dt::to_physical_value(1800.0, dt::Unit::s);
            req.remaining_time_to_bulk_soc = dt::to_physical_value(600.0, dt::Unit::s);
            secc.drive(req);

            THEN("They reach the module together with the completion flags") {
                REQUIRE(secc.charge_progress.size() == 1);
                REQUIRE(secc.charge_progress.front().remaining_time_to_full_soc.value() == 1800.0f);
                REQUIRE(secc.charge_progress.front().remaining_time_to_bulk_soc.value() == 600.0f);
                REQUIRE_FALSE(secc.charge_progress.front().charging_complete);
            }

            AND_WHEN("The EV repeats the same values") {
                secc.drive(req);

                THEN("Nothing new is reported") {
                    REQUIRE(secc.charge_progress.size() == 1);
                }
            }
        }
    }

    GIVEN("A machine in PreCharge") {
        Secc secc;
        to_pre_charge(secc);

        WHEN("The module reports that no insulation monitoring device is fitted") {
            secc.fsm.context().set_isolation_status(d20::IsolationStatus::NoImd);
            secc.drive(pre_charge_req());

            THEN("PreChargeRes carries No_IMD instead of the hardcoded Valid") {
                const auto res = secc.fsm.response<message_2::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->dc_evse_status.isolation_status.value() == dt::IsolationLevel::No_IMD);
            }
        }

        WHEN("The module reports an isolation warning") {
            secc.fsm.context().set_isolation_status(d20::IsolationStatus::Warning);
            secc.drive(pre_charge_req());

            THEN("PreChargeRes carries it") {
                const auto res = secc.fsm.response<message_2::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->dc_evse_status.isolation_status.value() == dt::IsolationLevel::Warning);
            }
        }
    }
}

SCENARIO("ISO 15118-2 SECC EV setpoint forwarding") {

    GIVEN("A machine in CurrentDemand") {
        Secc secc;
        to_current_demand(secc);
        secc.setpoints_forwarded = 0;

        WHEN("The EV repeats the same target three times") {
            secc.drive(current_demand_req());
            secc.drive(current_demand_req());
            secc.drive(current_demand_req());

            THEN("The setpoint is forwarded once (EvseV2G publish_dc_ev_target_voltage_current parity)") {
                REQUIRE(secc.setpoints_forwarded == 1);
            }

            AND_WHEN("The EV changes its target current") {
                auto req = current_demand_req();
                req.ev_target_current = amps(120.0);
                secc.drive(req);

                THEN("The new setpoint is forwarded") {
                    REQUIRE(secc.setpoints_forwarded == 2);
                }
            }

            AND_WHEN("The EV adds a maximum power limit") {
                auto req = current_demand_req();
                req.ev_maximum_power_limit = dt::to_physical_value(50000.0, dt::Unit::W);
                secc.drive(req);

                THEN("The changed limits are forwarded") {
                    REQUIRE(secc.setpoints_forwarded == 2);
                }
            }
        }
    }

    GIVEN("A machine in PreCharge") {
        Secc secc;
        to_pre_charge(secc);
        secc.setpoints_forwarded = 0;

        WHEN("The EV repeats the same pre-charge target") {
            secc.drive(pre_charge_req());
            secc.drive(pre_charge_req());

            THEN("The target is forwarded once") {
                REQUIRE(secc.setpoints_forwarded == 1);
            }
        }
    }
}

SCENARIO("ISO 15118-2 SECC isolation status in ChargeParameterDiscoveryRes") {

    GIVEN("A machine in ChargeParameterDiscovery before any cable check") {
        Secc secc;
        to_charge_parameter_discovery(secc);

        WHEN("The EV asks for DC charge parameters") {
            secc.drive(charge_parameter_req(dt::EnergyTransferMode::DC_extended));

            THEN("Isolation is reported Invalid: nothing has been verified yet") {
                const auto res = secc.fsm.response<message_2::ChargeParameterDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->dc_evse_charge_parameter->dc_evse_status.isolation_status.value() ==
                        dt::IsolationLevel::Invalid);
            }
        }
    }

    GIVEN("A charging machine whose module has reported a valid isolation") {
        Secc secc;
        to_current_demand(secc);
        secc.fsm.context().set_isolation_status(d20::IsolationStatus::Valid);

        WHEN("The EV renegotiates [V2G2-813] and asks for charge parameters again") {
            secc.drive(current_demand_req());
            secc.drive(power_delivery_req(dt::ChargeProgress::Renegotiate));
            REQUIRE(secc.fsm.state() == StateID::ChargeParameterDiscovery);
            secc.drive(charge_parameter_req(dt::EnergyTransferMode::DC_extended));

            THEN("The verified isolation is reported instead of a hardcoded Invalid") {
                const auto res = secc.fsm.response<message_2::ChargeParameterDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(res->dc_evse_charge_parameter->dc_evse_status.isolation_status.value() ==
                        dt::IsolationLevel::Valid);
            }
        }
    }
}

SCENARIO("ISO 15118-2 SECC no-energy pause") {

    Secc secc;

    // IEC 61851-23:2023 CC.3.5.3. Unlike DIN, ISO 15118-2 distinguishes the two stopping modes: only
    // BeforeCableCheck skips the cable check, and either way a PowerDelivery(Start) is refused.
    GIVEN("A machine in ChargeParameterDiscovery with no energy available at all") {
        to_charge_parameter_discovery(secc);
        secc.fsm.context().session_config.no_energy_pause = d20::NoEnergyPauseMode::BeforeCableCheck;

        WHEN("The EV asks for DC charge parameters") {
            secc.drive(charge_parameter_req(dt::EnergyTransferMode::DC_extended));

            THEN("The EV is told to stop and the cable check is skipped") {
                REQUIRE(secc.fsm.state() == StateID::PreChargeStart);
                const auto res = secc.fsm.response<message_2::ChargeParameterDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(res->dc_evse_charge_parameter->dc_evse_status.notification ==
                        dt::EVSENotification::StopCharging);
            }

            AND_WHEN("The EV ignores the stop and asks to start charging anyway") {
                // One PreChargeReq first: a PowerDeliveryReq is only in sequence from PreCharge [V2G2-587], so
                // without it the refusal under test would be masked by a sequence error.
                secc.drive(pre_charge_req());
                secc.drive(power_delivery_req(dt::ChargeProgress::Start));

                THEN("PowerDelivery is refused and the session ends") {
                    const auto res = secc.fsm.response<message_2::PowerDeliveryResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::FAILED);
                    REQUIRE(secc.fsm.context().session_stopped);
                }
            }

            AND_WHEN("The EV reacts with SessionStopReq") {
                secc.drive(message_2::SessionStopRequest{});

                // An EV told there is no energy before charging began has no in-sequence way to close the session;
                // the spec expects it to terminate the connection ([V2G2-728]), which the SECC reads as an error.
                THEN("It is out of sequence and the session ends with the error") {
                    REQUIRE(secc.fsm.state() == StateID::PreChargeStart);
                    const auto res = secc.fsm.response<message_2::SessionStopResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
                    REQUIRE(secc.fsm.context().session_stopped);
                }
            }
        }
    }

    GIVEN("A machine in ChargeParameterDiscovery where the EV may ignore the pause") {
        to_charge_parameter_discovery(secc);
        secc.fsm.context().session_config.no_energy_pause = d20::NoEnergyPauseMode::AllowEvToIgnorePause;

        WHEN("The EV asks for DC charge parameters") {
            secc.drive(charge_parameter_req(dt::EnergyTransferMode::DC_extended));

            THEN("The stop is signalled but the machine still moves on to CableCheck") {
                REQUIRE(secc.fsm.state() == StateID::CableCheck);
                const auto res = secc.fsm.response<message_2::ChargeParameterDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->dc_evse_charge_parameter->dc_evse_status.notification ==
                        dt::EVSENotification::StopCharging);
            }
        }
    }

    // The pause is a DC mechanism (EvseV2G parity): an AC EV never gets the StopCharging notification and
    // its PowerDelivery(Start) is not refused -- the 0 A limit in every ChargingStatusRes throttles it.
    GIVEN("An AC session with no energy available") {
        Secc ac_secc{dt::EnergyTransferMode::AC_three_phase_core};
        to_charge_parameter_discovery(ac_secc);
        ac_secc.fsm.context().session_config.no_energy_pause = d20::NoEnergyPauseMode::BeforeCableCheck;

        WHEN("The EV asks for AC charge parameters") {
            ac_secc.drive(charge_parameter_req(dt::EnergyTransferMode::AC_three_phase_core));

            THEN("No pause notification is sent") {
                REQUIRE(ac_secc.fsm.state() == StateID::AcPowerDelivery);
                const auto res = ac_secc.fsm.response<message_2::ChargeParameterDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->ac_evse_charge_parameter->ac_evse_status.notification == dt::EVSENotification::None);
            }

            AND_WHEN("The EV starts charging anyway") {
                ac_secc.drive(power_delivery_req(dt::ChargeProgress::Start, AC_PROFILE_POWER_W));
                ac_secc.fsm.control(d20::ClosedContactor{true});

                THEN("PowerDelivery is accepted") {
                    REQUIRE(ac_secc.fsm.state() == StateID::AcChargeLoopStart);
                    const auto res = ac_secc.fsm.response<message_2::PowerDeliveryResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::OK);
                    REQUIRE_FALSE(ac_secc.fsm.context().session_stopped);
                }
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
                REQUIRE(secc.fsm.state() == StateID::AcPowerDelivery);
                const auto res = secc.fsm.response<message_2::ChargeParameterDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }

        WHEN("The EV starts charging before the contactor is closed") {
            secc.drive(charge_parameter_req(dt::EnergyTransferMode::AC_three_phase_core));
            secc.drive(power_delivery_req(dt::ChargeProgress::Start, AC_PROFILE_POWER_W));

            THEN("The response is held back until the module reports the contactor closed") {
                REQUIRE(secc.fsm.state() == StateID::AcPowerDelivery);
                REQUIRE_FALSE(secc.fsm.has_response());
            }

            AND_WHEN("The contactor closes") {
                secc.fsm.control(d20::ClosedContactor{true});

                THEN("The held PowerDeliveryRes is sent and the machine moves to the charge loop") {
                    REQUIRE(secc.fsm.state() == StateID::AcChargeLoopStart);
                    const auto res = secc.fsm.response<message_2::PowerDeliveryResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::OK);
                }
            }
        }

        // [Table 104] makes RCD mandatory in every AC_EVSEStatus, and the receipt sits between two
        // ChargingStatusRes that both carry it. Asserted on a rejected receipt because a successful one
        // needs a signature the fixture PKI cannot produce.
        WHEN("The SECC asks for a signed receipt while the module reports an RCD error") {
            secc.drive(charge_parameter_req(dt::EnergyTransferMode::AC_three_phase_core));
            secc.drive(power_delivery_req(dt::ChargeProgress::Start, AC_PROFILE_POWER_W));
            secc.fsm.control(d20::ClosedContactor{true});

            secc.fsm.context().session_config.receipt_required = true;
            secc.fsm.context().set_contract_selected();
            dt::MeterInfo meter;
            meter.meter_id = "EVEREST";
            meter.meter_reading = 4242;
            secc.fsm.context().set_meter_info(meter);
            secc.fsm.context().set_active_error(d20::EvseErrorCode::RCD);

            secc.drive(message_2::ChargingStatusRequest{});
            REQUIRE(secc.fsm.state() == StateID::MeteringReceipt);

            AND_WHEN("The EV answers with a receipt") {
                message_2::MeteringReceiptRequest req;
                req.session_id = secc.session_id;
                secc.drive(req);

                THEN("The MeteringReceiptRes carries the RCD flag") {
                    const auto res = secc.fsm.response<message_2::MeteringReceiptResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->ac_evse_status.has_value());
                    REQUIRE(res->ac_evse_status->rcd == true);
                }
            }
        }

        // current_demand_started/-finished are DC-only: EvseManager acts on them by driving the DC supply
        // and the over-voltage monitor, so an AC charge loop must stay silent on both.
        WHEN("The EV runs an AC charge loop and ends it with PowerDeliveryReq(Stop)") {
            secc.drive(charge_parameter_req(dt::EnergyTransferMode::AC_three_phase_core));
            secc.drive(power_delivery_req(dt::ChargeProgress::Start, AC_PROFILE_POWER_W));
            secc.fsm.control(d20::ClosedContactor{true});
            secc.drive(message_2::ChargingStatusRequest{});
            secc.drive(power_delivery_req(dt::ChargeProgress::Stop));

            THEN("No charge loop start/finish is signalled, only the contactor opens") {
                REQUIRE(secc.fsm.state() == StateID::SessionStop);
                REQUIRE_FALSE(secc.saw_signal(session::feedback::Signal::CHARGE_LOOP_STARTED));
                REQUIRE_FALSE(secc.saw_signal(session::feedback::Signal::CHARGE_LOOP_FINISHED));
                REQUIRE(secc.saw_signal(session::feedback::Signal::AC_OPEN_CONTACTOR));
            }
        }
    }
}

SCENARIO("ISO 15118-2 SECC Plug-and-Charge state transitions") {

    std::optional<std::string> forwarded_exi_request;
    std::optional<shared_datatypes::PaymentOption> reported_payment_option;
    session::feedback::Callbacks callbacks;
    callbacks.certificate_request = [&forwarded_exi_request](const std::string& exi_request_base64,
                                                             session::feedback::CertificateExchangeAction) {
        forwarded_exi_request = exi_request_base64;
    };
    callbacks.selected_payment_option = [&reported_payment_option](shared_datatypes::PaymentOption option) {
        reported_payment_option = option;
    };

    Secc secc{make_pnc_config(), callbacks};

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

    GIVEN("A machine in ServiceSelection with PnC enabled") {
        secc.drive_session_setup();
        secc.drive(message_2::ServiceDiscoveryRequest{});
        REQUIRE(secc.fsm.state() == StateID::ServiceSelection);

        WHEN("The EV selects Contract payment") {
            secc.drive(contract_selection_req());

            THEN("The machine moves to PaymentDetails instead of Authorization [V2G2-432]") {
                REQUIRE(secc.fsm.state() == StateID::Identification);
                const auto res = secc.fsm.response<message_2::PaymentServiceSelectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(secc.fsm.context().session().contract_selected);
                REQUIRE(secc.fsm.context().session().cert_install_selected);
                REQUIRE(reported_payment_option == shared_datatypes::PaymentOption::Contract);
            }
        }
    }

    GIVEN("A machine in PaymentDetails") {
        to_payment_details(true);
        REQUIRE(secc.fsm.state() == StateID::Identification);

        WHEN("The EV sends a contract certificate that does not parse") {
            message_2::PaymentDetailsRequest req;
            req.emaid = "DEPNX123456789"; // 14-15 characters, or the EXI document does not decode
            req.contract_certificate = {0x01, 0x02, 0x03};
            secc.drive(req);

            THEN("The chain is rejected and the session ends without a transition") {
                REQUIRE(secc.fsm.state() == StateID::Identification);
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
                REQUIRE(secc.fsm.state() == StateID::Identification);
                REQUIRE(forwarded_exi_request.has_value());
                REQUIRE_FALSE(forwarded_exi_request->empty());
                // Nothing is answered until the backend responds.
                REQUIRE_FALSE(secc.fsm.has_response());
            }

            AND_WHEN("The backend returns a CertificateInstallationRes") {
                secc.fsm.control(d20::CertificateResponse{true, "3q2+7w=="}); // arbitrary EXI bytes

                THEN("The raw response is spliced and the machine moves on to PaymentDetails [V2G2-554]") {
                    REQUIRE(secc.fsm.state() == StateID::PaymentDetails);
                    REQUIRE(secc.fsm.has_response());
                }
            }

            AND_WHEN("The backend answers and the EV asks for a second certificate exchange") {
                secc.fsm.control(d20::CertificateResponse{true, "3q2+7w=="});
                REQUIRE(secc.fsm.state() == StateID::PaymentDetails);
                secc.drive(certificate_installation_req());

                // Node 6 accepts PaymentDetailsReq alone [V2G2-554]: the optional excursion is used up.
                THEN("It is out of sequence") {
                    REQUIRE(secc.fsm.state() == StateID::PaymentDetails);
                    const auto res = secc.fsm.response<message_2::CertificateInstallationResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
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
        REQUIRE(secc.fsm.state() == StateID::Identification);

        WHEN("The EV runs a certificate installation anyway") {
            secc.drive(certificate_installation_req());

            THEN("The unselected action is out of sequence [V2G2-539]") {
                REQUIRE(secc.fsm.state() == StateID::Identification);
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

SCENARIO("ISO 15118-2 SECC external VAS providers") {
    namespace m20dt = message_20::datatypes;

    auto config = make_config(dt::EnergyTransferMode::DC_extended);
    dt::Service vas;
    vas.service_id = 42;
    vas.service_name = "Parking";
    vas.service_category = dt::ServiceCategory::OtherCustom;
    vas.free_service = true;
    config.offered_vas_services.push_back(vas);

    std::vector<uint16_t> detail_requests;
    std::optional<m20dt::VasSelectedServiceList> selected;
    session::feedback::Callbacks callbacks;
    callbacks.get_vas_parameters = [&](uint16_t service_id) -> std::optional<m20dt::ServiceParameterList> {
        detail_requests.push_back(service_id);
        m20dt::ServiceParameterList list;
        auto& set = list.emplace_back();
        set.id = 7;
        set.parameter.push_back({"Slot", static_cast<int32_t>(3)});
        set.parameter.push_back({"Name", m20dt::Name{"Lot A"}});
        set.parameter.push_back({"Fee", m20dt::from_float(1.5f)});
        return list;
    };
    callbacks.selected_vas_services = [&](const m20dt::VasSelectedServiceList& list) { selected = list; };

    GIVEN("A session with one external VAS offered") {
        Secc secc(config, std::move(callbacks));
        to_service_detail(secc);

        THEN("ServiceDiscoveryRes lists the service") {
            const auto res = secc.fsm.response<message_2::ServiceDiscoveryResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res->service_list.has_value());
            REQUIRE(res->service_list->size() == 1);
            REQUIRE(res->service_list->front().service_id == 42);
            REQUIRE(res->service_list->front().service_name == "Parking");
        }

        WHEN("The EV asks for its details") {
            message_2::ServiceDetailRequest req;
            req.service_id = 42;
            secc.drive(req);

            THEN("The provider is asked and its parameter sets are translated into the response") {
                REQUIRE(detail_requests == std::vector<uint16_t>{42});
                const auto res = secc.fsm.response<message_2::ServiceDetailResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(res->service_parameter_list.has_value());
                const auto& set = res->service_parameter_list->front();
                REQUIRE(set.parameter_set_id == 7);
                REQUIRE(set.parameter.size() == 3);
                REQUIRE(set.parameter[0].int_value == 3);
                REQUIRE(set.parameter[1].string_value == "Lot A");
                REQUIRE(set.parameter[2].physical_value.has_value());
                REQUIRE(set.parameter[2].physical_value->unit == dt::Unit::W);
                REQUIRE(secc.fsm.state() == StateID::ServiceSelection);
            }

            AND_WHEN("The EV selects the charge service and the VAS with parameter set 7") {
                auto selection = payment_selection_req();
                selection.selected_service_list.push_back({42, 7});
                secc.drive(selection);

                THEN("OK, the provider is told about the selection and the machine moves on") {
                    const auto res = secc.fsm.response<message_2::PaymentServiceSelectionResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::OK);
                    REQUIRE(selected.has_value());
                    REQUIRE(selected->size() == 1);
                    REQUIRE(selected->front().service_id == 42);
                    REQUIRE(selected->front().parameter_set_id == 7);
                    REQUIRE(secc.fsm.state() == StateID::Authorization);
                }
            }
        }

        WHEN("The EV selects a VAS that was never offered") {
            auto selection = payment_selection_req();
            selection.selected_service_list.push_back({43, std::nullopt});
            secc.drive(selection);

            THEN("FAILED_ServiceSelectionInvalid, no provider callback, session stopped") {
                const auto res = secc.fsm.response<message_2::PaymentServiceSelectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_ServiceSelectionInvalid);
                REQUIRE_FALSE(selected.has_value());
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }
    }
}

SCENARIO("ISO 15118-2 SECC charger stop and shutdown paths") {
    Secc secc;

    GIVEN("A machine in PreCharge") {
        to_pre_charge(secc);

        // The engine latches a StopCharging control event in ANY state; the harness sets the flag likewise.
        WHEN("The module requests a stop during pre-charge") {
            secc.fsm.context().set_charger_stop_requested(true);
            secc.drive(pre_charge_req());

            THEN("The stop is signalled already in the PreChargeRes") {
                const auto res = secc.fsm.response<message_2::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->dc_evse_status.notification == dt::EVSENotification::StopCharging);
                REQUIRE(res->dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Shutdown);
            }

            AND_WHEN("The EV ignores it and enters the charge loop") {
                secc.drive(power_delivery_req(dt::ChargeProgress::Start));
                secc.drive(current_demand_req());

                THEN("The latched stop is still signalled in the CurrentDemandRes") {
                    const auto res = secc.fsm.response<message_2::CurrentDemandResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->dc_evse_status.notification == dt::EVSENotification::StopCharging);
                    REQUIRE(res->dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Shutdown);
                }
            }
        }

        WHEN("The EV keeps the session going beyond the stop-charging guard") {
            secc.fsm.context().set_charger_stop_requested(true);
            secc.fsm.context().charger_stop_ignored = true; // set by the engine on the STOP_CHARGING timeout
            secc.drive(pre_charge_req());

            THEN("The stop is enforced: the response is FAILED and the session terminates") {
                const auto res = secc.fsm.response<message_2::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }

        // [V2G2-539]: answer FAILED and terminate with that response rather than drop the TCP connection
        // and leave the EV without a reason.
        WHEN("The module reports an emergency shutdown") {
            secc.fsm.context().set_active_error(d20::EvseErrorCode::EmergencyShutdown);
            secc.fsm.context().set_emergency_shutdown();
            secc.drive(pre_charge_req());

            THEN("The next response is FAILED, carries EVSE_EmergencyShutdown, and ends the session") {
                const auto res = secc.fsm.response<message_2::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED);
                REQUIRE(res->dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_EmergencyShutdown);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }

        // [V2G2-880]: every other status code is informational, so the EV decides (EvseV2G parity).
        WHEN("The module reports a malfunction") {
            secc.fsm.context().set_active_error(d20::EvseErrorCode::Malfunction);
            secc.drive(pre_charge_req());

            THEN("The response reports EVSE_Malfunction but stays OK and the session continues") {
                const auto res = secc.fsm.response<message_2::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(res->dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Malfunction);
                REQUIRE_FALSE(secc.fsm.context().session_stopped);
            }

            AND_WHEN("The EV then asks to start power delivery") {
                secc.drive(power_delivery_req(dt::ChargeProgress::Start));

                THEN("FAILED_PowerDeliveryNotApplied [V2G2-480]") {
                    const auto res = secc.fsm.response<message_2::PowerDeliveryResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::FAILED_PowerDeliveryNotApplied);
                }
            }
        }
    }
}

// The SessionID rule [V2G2-460] holds in every state, so StateBase::feed() checks it once for all of
// them. These pin the two consequences that are not obvious: which error wins when a message breaks
// both rules at once, and the two cases that must fall through to the per-state sequence check.
SCENARIO("ISO 15118-2 SECC SessionID validation") {
    GIVEN("An SECC that has not assigned a SessionID yet") {
        Secc secc;

        WHEN("The first message is not a SessionSetupReq") {
            secc.drive(message_2::ServiceDiscoveryRequest{});

            // No session exists, so there is nothing to compare against -- and Table 112 does not list
            // FAILED_UnknownSession for SessionSetupRes at all, leaving only the sequence error.
            THEN("FAILED_SequenceError, not FAILED_UnknownSession") {
                const auto res = secc.fsm.response<message_2::ServiceDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
            }
        }
    }

    GIVEN("A negotiated session sitting in Authorization") {
        Secc secc;
        to_authorization(secc);

        WHEN("A request arrives that is both out of sequence and carries a foreign SessionID") {
            secc.drive_wrong_session(current_demand_req());

            // Both [V2G2-459] and [V2G2-460] apply and the spec orders neither. The SessionID is a header
            // field, so the envelope is answered before the content.
            THEN("The SessionID error wins") {
                const auto res = secc.fsm.response<message_2::CurrentDemandResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_UnknownSession);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }

        WHEN("A SessionSetupReq arrives mid-session with a foreign SessionID") {
            secc.drive_wrong_session(message_2::SessionSetupRequest{});

            // [V2G2-460] exempts SessionSetupReq by name, so this falls through to the state's sequence check.
            THEN("FAILED_SequenceError, not FAILED_UnknownSession") {
                const auto res = secc.fsm.response<message_2::SessionSetupResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
            }
        }
    }

    GIVEN("A DC session waiting in the pre-charge loop") {
        Secc secc;
        to_pre_charge_loop(secc);

        WHEN("A foreign-session PowerDeliveryReq arrives") {
            secc.drive_wrong_session(power_delivery_req(dt::ChargeProgress::Start));

            // Regression pin: the old catch-all divert ran before the SessionID was looked at, so a request
            // naming another session moved the state machine on.
            THEN("The state does not advance and the request is rejected") {
                REQUIRE(secc.fsm.state() == StateID::PreCharge);
                const auto res = secc.fsm.response<message_2::PowerDeliveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_UnknownSession);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }
    }
}

// Regression pin: a loop state used to hand every unrecognised request to the state it can
// transition to -- the same response, but after a transition the spec does not allow.
SCENARIO("ISO 15118-2 SECC out-of-sequence requests are answered by the state that received them") {
    GIVEN("A session in ServiceSelection") {
        Secc secc;
        to_service_detail(secc);
        REQUIRE(secc.fsm.state() == StateID::ServiceSelection);

        WHEN("A request arrives that is neither ServiceDetailReq nor PaymentServiceSelectionReq") {
            secc.drive(message_2::AuthorizationRequest{});

            THEN("ServiceSelection answers FAILED_SequenceError and does not advance") {
                REQUIRE(secc.fsm.state() == StateID::ServiceSelection);
                const auto res = secc.fsm.response<message_2::AuthorizationResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }
    }

    GIVEN("A DC session in the pre-charge loop") {
        Secc secc;
        to_pre_charge_loop(secc);
        REQUIRE(secc.fsm.state() == StateID::PreCharge);

        WHEN("A request arrives that is neither PreChargeReq nor PowerDeliveryReq") {
            secc.drive(message_2::CableCheckRequest{});

            THEN("PreCharge answers FAILED_SequenceError and does not advance") {
                REQUIRE(secc.fsm.state() == StateID::PreCharge);
                const auto res = secc.fsm.response<message_2::CableCheckResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }
    }

    GIVEN("A DC session in the CurrentDemand charge loop") {
        Secc secc;
        to_current_demand(secc);
        REQUIRE(secc.fsm.state() == StateID::DcChargeLoop);

        WHEN("A request arrives that is none of CurrentDemandReq, MeteringReceiptReq, PowerDeliveryReq") {
            secc.drive(message_2::ServiceDiscoveryRequest{});

            THEN("CurrentDemand answers FAILED_SequenceError and does not advance") {
                REQUIRE(secc.fsm.state() == StateID::DcChargeLoop);
                const auto res = secc.fsm.response<message_2::ServiceDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }
    }

    // Figure 104 draws welding detection as one bubble, but the requirements narrow it: [V2G2-601]
    // admits a restart after PowerDelivery(Stop), [V2G2-597] does not once it has begun.
    GIVEN("A DC session that has just stopped power delivery") {
        Secc secc;
        to_welding_detection(secc);
        REQUIRE(secc.fsm.state() == StateID::PostCharge);

        WHEN("The EV restarts the parameter exchange instead of weld-detecting") {
            secc.drive(charge_parameter_req(dt::EnergyTransferMode::DC_extended));

            // PowerDelivery(Stop) opened the contactor, so the isolation test has to run again (8.7.4.3 NOTE 1).
            THEN("It is in sequence [V2G2-601] and the cable check is re-run") {
                REQUIRE(secc.fsm.state() == StateID::CableCheck);
                const auto res = secc.fsm.response<message_2::ChargeParameterDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }

        AND_WHEN("The EV weld-detects first and only then tries to restart") {
            secc.drive(message_2::WeldingDetectionRequest{});
            REQUIRE(secc.fsm.state() == StateID::WeldingDetection);
            secc.drive(charge_parameter_req(dt::EnergyTransferMode::DC_extended));

            THEN("The restart is no longer in sequence [V2G2-597]") {
                REQUIRE(secc.fsm.state() == StateID::WeldingDetection);
                const auto res = secc.fsm.response<message_2::ChargeParameterDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
            }
        }
    }

    GIVEN("An AC session in the charge loop") {
        Secc secc{dt::EnergyTransferMode::AC_three_phase_core};
        to_ac_charge_loop(secc);
        REQUIRE(secc.fsm.state() == StateID::AcChargeLoop);

        WHEN("A request arrives that is neither ChargingStatusReq nor PowerDeliveryReq") {
            secc.drive(message_2::CableCheckRequest{});

            THEN("AcChargeLoop answers FAILED_SequenceError and does not advance") {
                REQUIRE(secc.fsm.state() == StateID::AcChargeLoop);
                const auto res = secc.fsm.response<message_2::CableCheckResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }
    }
}

// Figures 103/104 place SessionStopReq in two states only, both entered after a PowerDeliveryRes
// with ChargeProgress=Stop; [V2G2-538] makes it a sequence error everywhere else.
SCENARIO("ISO 15118-2 SECC accepts SessionStopReq only after PowerDelivery(Stop)") {
    GIVEN("A DC session in the charge loop") {
        Secc secc;
        to_current_demand(secc);
        secc.drive(current_demand_req());
        REQUIRE(secc.fsm.state() == StateID::DcChargeLoop);

        WHEN("The EV sends SessionStopReq without stopping power delivery first") {
            secc.drive(message_2::SessionStopRequest{});

            THEN("It is out of sequence and the loop does not advance") {
                REQUIRE(secc.fsm.state() == StateID::DcChargeLoop);
                const auto res = secc.fsm.response<message_2::SessionStopResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
            }
        }
    }

    GIVEN("An AC session in the charge loop, for SessionStopReq placement") {
        Secc secc{dt::EnergyTransferMode::AC_three_phase_core};
        to_ac_charge_loop(secc);
        REQUIRE(secc.fsm.state() == StateID::AcChargeLoop);

        WHEN("The EV sends SessionStopReq without stopping power delivery first") {
            secc.drive(message_2::SessionStopRequest{});

            THEN("It is out of sequence and the loop does not advance") {
                REQUIRE(secc.fsm.state() == StateID::AcChargeLoop);
                const auto res = secc.fsm.response<message_2::SessionStopResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
            }
        }

        WHEN("The EV stops power delivery first") {
            secc.drive(power_delivery_req(dt::ChargeProgress::Stop));
            REQUIRE(secc.fsm.state() == StateID::SessionStop);

            AND_WHEN("It then signals CP State B and sends SessionStopReq") {
                // [V2G2-913]: PowerDelivery(Stop) arms the CP State B gate, so the request is parked until B.
                secc.fsm.context().set_cp_state(d20::CpState::B);
                secc.drive(message_2::SessionStopRequest{});

                THEN("It is in sequence and answered OK [V2G2-568]") {
                    const auto res = secc.fsm.response<message_2::SessionStopResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::OK);
                    REQUIRE(secc.fsm.context().session_stopped);
                }
            }
        }
    }
}

// [V2G2-920] names both request types, so PostCharge gates a SessionStopReq as well. That gate used
// to be applied by SessionStop, which PostCharge transitioned into, and had to move here once the
// answer became a plain function call; this pins the behaviour against the node that now owns it.
SCENARIO("ISO 15118-2 SECC gates a post-charge SessionStopReq on CP State B") {
    GIVEN("A DC session that has stopped power delivery, with the EV not yet back in CP State B") {
        Secc secc;
        to_current_demand(secc);
        secc.drive(current_demand_req());
        secc.drive(power_delivery_req(dt::ChargeProgress::Stop));
        REQUIRE(secc.fsm.state() == StateID::PostCharge);

        WHEN("The EV ends the session straight away") {
            secc.drive(message_2::SessionStopRequest{});

            THEN("The answer is held back until CP State B is measured [V2G2-920]") {
                REQUIRE(secc.fsm.state() == StateID::PostCharge);
                REQUIRE_FALSE(secc.fsm.has_response());
                REQUIRE_FALSE(secc.fsm.context().session_stopped);
            }

            AND_WHEN("The EV signals CP State B") {
                secc.fsm.context().set_cp_state(d20::CpState::B);
                secc.fsm.control(d20::CpStateChanged{d20::CpState::B});

                THEN("The held SessionStopRes goes out OK and the session ends [V2G2-921]") {
                    const auto res = secc.fsm.response<message_2::SessionStopResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::OK);
                    REQUIRE(secc.fsm.context().session_stopped);
                }
            }

            AND_WHEN("CP State B never arrives and the gate expires") {
                secc.fsm.timeout(d20::TimeoutType::CPSTATE);

                THEN("SessionStopRes/FAILED ends the session [V2G2-922]") {
                    const auto res = secc.fsm.response<message_2::SessionStopResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::FAILED);
                    REQUIRE(secc.fsm.context().session_stopped);
                }
            }
        }
    }
}
