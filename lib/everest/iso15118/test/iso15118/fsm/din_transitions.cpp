// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// FSM-level tests of the DIN SPEC 70121 SECC state machine: every state is entered for real and the
// transition it takes is asserted, which the response-builder tests in states/din/ cannot cover. The
// counterpart of d20_transitions.cpp for the -20 state machine.
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

#include "secc_helper.hpp"

#include <iso15118/din/state/session_setup.hpp>
#include <iso15118/message_din/cable_check.hpp>
#include <iso15118/message_din/charge_parameter_discovery.hpp>
#include <iso15118/message_din/contract_authentication.hpp>
#include <iso15118/message_din/current_demand.hpp>
#include <iso15118/message_din/power_delivery.hpp>
#include <iso15118/message_din/pre_charge.hpp>
#include <iso15118/message_din/service_discovery.hpp>
#include <iso15118/message_din/service_payment_selection.hpp>
#include <iso15118/message_din/session_setup.hpp>
#include <iso15118/message_din/session_stop.hpp>
#include <iso15118/message_din/welding_detection.hpp>

using namespace iso15118;
using iso15118::test::DinSeccFsm;

namespace dt = message_din::datatypes;
using StateID = din::StateID;

namespace {

constexpr uint16_t CHARGE_SERVICE_ID = 1;

din::SessionConfig make_config() {
    din::SessionConfig config;
    config.evse_id = {0x12, 0x34};
    config.charge_service_id = CHARGE_SERVICE_ID;
    config.evse_maximum_current_limit = 400.0;
    config.evse_maximum_power_limit = 150000.0;
    config.evse_maximum_voltage_limit = 920.0;
    config.evse_minimum_current_limit = 0.0;
    config.evse_minimum_voltage_limit = 0.0;
    return config;
}

// The state machine under test plus the session id the SECC assigned, stamped into every following
// request the way a real EVCC echoes it back.
class Secc {
public:
    template <typename Request> void drive(Request request) {
        request.header.session_id = session_id;
        fsm.drive(request);
    }

    // SessionSetupReq is the only request that carries no assigned session id yet.
    void drive_session_setup() {
        message_din::SessionSetupRequest req;
        req.evcc_id = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
        fsm.drive(req);
        session_id = fsm.context().get_session_id();
    }

    DinSeccFsm fsm{make_config()};
    dt::SessionId session_id{};
};

message_din::ServicePaymentSelectionRequest payment_selection_req() {
    message_din::ServicePaymentSelectionRequest req;
    req.selected_payment_option = dt::PaymentOption::ExternalPayment;
    req.selected_service_list.push_back({CHARGE_SERVICE_ID, std::nullopt});
    return req;
}

message_din::ChargeParameterDiscoveryRequest charge_parameter_req() {
    message_din::ChargeParameterDiscoveryRequest req;
    req.ev_requested_energy_transfer_type = dt::EnergyTransferMode::DC_extended;
    auto& params = req.dc_ev_charge_parameter.emplace();
    params.ev_maximum_current_limit = 200.0;
    params.ev_maximum_voltage_limit = 400.0;
    return req;
}

message_din::PowerDeliveryRequest power_delivery_req(bool ready_to_charge) {
    message_din::PowerDeliveryRequest req;
    req.ready_to_charge_state = ready_to_charge;
    return req;
}

message_din::PreChargeRequest pre_charge_req() {
    message_din::PreChargeRequest req;
    req.ev_target_voltage = 400.0;
    req.ev_target_current = 2.0;
    return req;
}

message_din::CurrentDemandRequest current_demand_req() {
    message_din::CurrentDemandRequest req;
    req.ev_target_voltage = 400.0;
    req.ev_target_current = 100.0;
    return req;
}

// --- advance helpers: drive the session up to (and not into) the state under test ---

void to_service_discovery(Secc& secc) {
    secc.drive_session_setup();
}

void to_payment_selection(Secc& secc) {
    to_service_discovery(secc);
    secc.drive(message_din::ServiceDiscoveryRequest{});
}

void to_contract_authentication(Secc& secc) {
    to_payment_selection(secc);
    secc.drive(payment_selection_req());
}

void to_charge_parameter_discovery(Secc& secc) {
    to_contract_authentication(secc);
    secc.drive(message_din::ContractAuthenticationRequest{});
    secc.fsm.control(d20::AuthorizationResponse{true});
    secc.drive(message_din::ContractAuthenticationRequest{});
}

void to_cable_check(Secc& secc) {
    to_charge_parameter_discovery(secc);
    secc.drive(charge_parameter_req());
    // [V2G-DC-967]: the cable check only runs once the EV signals CP State C/D.
    secc.fsm.context().current_cp_state = d20::CpState::C;
}

void to_pre_charge(Secc& secc) {
    to_cable_check(secc);
    secc.drive(message_din::CableCheckRequest{});
    secc.fsm.control(d20::CableCheckFinished{true});
    secc.drive(message_din::CableCheckRequest{});
}

void to_current_demand(Secc& secc) {
    to_pre_charge(secc);
    secc.drive(pre_charge_req());
    secc.drive(power_delivery_req(true));
}

void to_welding_detection(Secc& secc) {
    to_current_demand(secc);
    secc.drive(current_demand_req());
    secc.drive(power_delivery_req(false));
    // [V2G-DC-988]: after PowerDelivery(off) the EV signals CP State B again.
    secc.fsm.context().current_cp_state = d20::CpState::B;
}

} // namespace

SCENARIO("DIN SPEC 70121 SECC handshake state transitions") {

    Secc secc;

    GIVEN("A fresh session machine") {
        REQUIRE(secc.fsm.state() == StateID::SessionSetup);

        WHEN("The EV sends SessionSetupReq") {
            secc.drive_session_setup();

            THEN("The session is established and the machine moves to ServiceDiscovery") {
                REQUIRE(secc.fsm.state() == StateID::ServiceDiscovery);
                const auto res = secc.fsm.response<message_din::SessionSetupResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK_NewSessionEstablished);
                // [V2G-DC-872]: a freshly generated, non-zero session id is assigned.
                REQUIRE(secc.session_id != dt::SessionId{});
            }
        }

        WHEN("The EV skips SessionSetup and sends a charge loop request") {
            secc.fsm.drive(message_din::CurrentDemandRequest{});

            THEN("The session is sequence-errored and stays put [V2G-DC-539]") {
                REQUIRE(secc.fsm.state() == StateID::SessionSetup);
                const auto res = secc.fsm.response<message_din::CurrentDemandResponse>();
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
            secc.drive(message_din::ServiceDiscoveryRequest{});

            THEN("The charge service is offered and the machine moves to ServicePaymentSelection") {
                REQUIRE(secc.fsm.state() == StateID::ServicePaymentSelection);
                const auto res = secc.fsm.response<message_din::ServiceDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(res->charge_service.service_tag.service_id == CHARGE_SERVICE_ID);
            }
        }
    }

    GIVEN("A machine in ServicePaymentSelection") {
        to_payment_selection(secc);
        REQUIRE(secc.fsm.state() == StateID::ServicePaymentSelection);

        WHEN("The EV selects ExternalPayment and the charge service") {
            secc.drive(payment_selection_req());

            THEN("The machine moves to ContractAuthentication") {
                REQUIRE(secc.fsm.state() == StateID::ContractAuthentication);
                const auto res = secc.fsm.response<message_din::ServicePaymentSelectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }
    }

    GIVEN("A machine in ContractAuthentication") {
        to_contract_authentication(secc);
        REQUIRE(secc.fsm.state() == StateID::ContractAuthentication);

        WHEN("Authorization has not been granted yet") {
            secc.drive(message_din::ContractAuthenticationRequest{});

            THEN("The EV is kept polling and the machine stays in ContractAuthentication") {
                REQUIRE(secc.fsm.state() == StateID::ContractAuthentication);
                const auto res = secc.fsm.response<message_din::ContractAuthenticationResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(res->evse_processing == dt::EvseProcessing::Ongoing);
            }
        }

        WHEN("The module authorizes the session") {
            secc.drive(message_din::ContractAuthenticationRequest{});
            secc.fsm.control(d20::AuthorizationResponse{true});

            THEN("The control event alone does not move the machine") {
                REQUIRE(secc.fsm.state() == StateID::ContractAuthentication);
            }

            AND_WHEN("The EV polls again") {
                secc.drive(message_din::ContractAuthenticationRequest{});

                THEN("Processing finishes and the machine moves to ChargeParameterDiscovery") {
                    REQUIRE(secc.fsm.state() == StateID::ChargeParameterDiscovery);
                    const auto res = secc.fsm.response<message_din::ContractAuthenticationResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->evse_processing == dt::EvseProcessing::Finished);
                }
            }
        }

        WHEN("The EV aborts with SessionStopReq") {
            secc.drive(message_din::SessionStopRequest{});

            THEN("The machine hands over to SessionStop and answers it") {
                REQUIRE(secc.fsm.state() == StateID::SessionStop);
                const auto res = secc.fsm.response<message_din::SessionStopResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }
    }
}

SCENARIO("DIN SPEC 70121 SECC charging state transitions") {

    Secc secc;

    GIVEN("A machine in ChargeParameterDiscovery") {
        to_charge_parameter_discovery(secc);
        REQUIRE(secc.fsm.state() == StateID::ChargeParameterDiscovery);

        WHEN("The EV asks for DC_extended charge parameters") {
            secc.drive(charge_parameter_req());

            THEN("The parameters are answered as Finished and the machine moves to CableCheck") {
                REQUIRE(secc.fsm.state() == StateID::CableCheck);
                const auto res = secc.fsm.response<message_din::ChargeParameterDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(res->evse_processing == dt::EvseProcessing::Finished);
            }
        }
    }

    GIVEN("A machine in CableCheck with the EV in CP State C") {
        to_cable_check(secc);
        REQUIRE(secc.fsm.state() == StateID::CableCheck);

        WHEN("The cable check has not finished yet") {
            secc.drive(message_din::CableCheckRequest{});

            THEN("The EV is kept polling and the machine stays in CableCheck") {
                REQUIRE(secc.fsm.state() == StateID::CableCheck);
                const auto res = secc.fsm.response<message_din::CableCheckResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->evse_processing == dt::EvseProcessing::Ongoing);
            }
        }

        WHEN("The module reports the cable check finished") {
            secc.drive(message_din::CableCheckRequest{});
            secc.fsm.control(d20::CableCheckFinished{true});
            secc.drive(message_din::CableCheckRequest{});

            THEN("The machine moves to PreCharge") {
                REQUIRE(secc.fsm.state() == StateID::PreCharge);
                const auto res = secc.fsm.response<message_din::CableCheckResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->evse_processing == dt::EvseProcessing::Finished);
            }
        }
    }

    GIVEN("A machine in CableCheck without a reported CP State C/D") {
        to_charge_parameter_discovery(secc);
        secc.drive(charge_parameter_req());
        REQUIRE(secc.fsm.state() == StateID::CableCheck);

        WHEN("The EV sends CableCheckReq") {
            secc.drive(message_din::CableCheckRequest{});

            THEN("The request is parked without a response until CP State C/D is seen [V2G-DC-967]") {
                REQUIRE(secc.fsm.state() == StateID::CableCheck);
                REQUIRE_FALSE(secc.fsm.has_response());
            }

            AND_WHEN("CP State C is reported") {
                secc.fsm.context().current_cp_state = d20::CpState::C;
                secc.fsm.control(d20::CableCheckFinished{true});

                THEN("The parked request is answered and the machine moves to PreCharge") {
                    REQUIRE(secc.fsm.state() == StateID::PreCharge);
                    const auto res = secc.fsm.response<message_din::CableCheckResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->evse_processing == dt::EvseProcessing::Finished);
                }
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
                const auto res = secc.fsm.response<message_din::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }

        WHEN("The EV ends pre-charge with PowerDeliveryReq(ready)") {
            secc.drive(pre_charge_req());
            secc.drive(power_delivery_req(true));

            THEN("PreCharge defers to PowerDelivery, which answers and starts the charge loop") {
                // Peek-and-divert: PreCharge hands the still-pending request to PowerDelivery, which
                // answers it and moves on to CurrentDemand -- all within one received message.
                REQUIRE(secc.fsm.state() == StateID::CurrentDemand);
                const auto res = secc.fsm.response<message_din::PowerDeliveryResponse>();
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
                const auto res = secc.fsm.response<message_din::CurrentDemandResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }

        WHEN("The EV stops charging with PowerDeliveryReq(not ready)") {
            secc.drive(current_demand_req());
            secc.drive(power_delivery_req(false));

            THEN("The charge loop defers to PowerDelivery and the machine moves to WeldingDetection") {
                REQUIRE(secc.fsm.state() == StateID::WeldingDetection);
                const auto res = secc.fsm.response<message_din::PowerDeliveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                // The contactor is open again, so the verified isolation no longer holds.
                REQUIRE_FALSE(secc.fsm.context().cable_check_done);
            }
        }
    }

    GIVEN("A machine in WeldingDetection with the EV back in CP State B") {
        to_welding_detection(secc);
        REQUIRE(secc.fsm.state() == StateID::WeldingDetection);

        WHEN("The EV runs welding detection") {
            secc.drive(message_din::WeldingDetectionRequest{});

            THEN("The machine stays in WeldingDetection") {
                REQUIRE(secc.fsm.state() == StateID::WeldingDetection);
                const auto res = secc.fsm.response<message_din::WeldingDetectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }

        WHEN("The EV ends the session") {
            secc.drive(message_din::WeldingDetectionRequest{});
            secc.drive(message_din::SessionStopRequest{});

            THEN("WeldingDetection defers to SessionStop, which answers and ends the session") {
                REQUIRE(secc.fsm.state() == StateID::SessionStop);
                const auto res = secc.fsm.response<message_din::SessionStopResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }
    }
}
