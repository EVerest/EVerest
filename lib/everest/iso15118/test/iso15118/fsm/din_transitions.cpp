// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// FSM-level tests of the DIN SPEC 70121 SECC state machine: every state is entered for real and the
// transition it takes is asserted, which the response-builder tests in states/din/ cannot cover. The
// counterpart of d20_transitions.cpp for the -20 state machine.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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
// request the way a real EVCC echoes it back. Feedback signals and forwarded EV setpoints are
// recorded so the tests can assert which side effects (do not) fire.
class Secc {
public:
    Secc() : fsm(make_config(), make_callbacks()) {
    }

    template <typename Request> void drive(Request request) {
        request.header.session_id = session_id;
        fsm.drive(request);
    }

    // Drives the request with a SessionID that does not match the assigned one [V2G-DC-391].
    template <typename Request> void drive_wrong_session(Request request) {
        request.header.session_id = session_id;
        request.header.session_id[0] ^= 0xFF;
        fsm.drive(request);
    }

    // SessionSetupReq is the only request that carries no assigned session id yet.
    void drive_session_setup() {
        message_din::SessionSetupRequest req;
        req.evcc_id = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
        fsm.drive(req);
        session_id = fsm.context().get_session_id();
    }

    bool saw_signal(session::feedback::Signal signal) const {
        return std::find(signals.begin(), signals.end(), signal) != signals.end();
    }

    std::vector<session::feedback::Signal> signals;
    std::optional<shared_datatypes::PaymentOption> selected_payment_option;
    int setpoints_forwarded{0};
    std::optional<session::feedback::EvChargeParameters> ev_charge_parameters;
    std::optional<session::feedback::DcMaximumLimits> dc_max_limits;
    std::vector<session::feedback::DcEvChargeProgress> charge_progress;
    DinSeccFsm fsm;
    dt::SessionId session_id{};

private:
    session::feedback::Callbacks make_callbacks() {
        session::feedback::Callbacks callbacks;
        callbacks.signal = [this](session::feedback::Signal signal) { signals.push_back(signal); };
        callbacks.selected_payment_option = [this](shared_datatypes::PaymentOption option) {
            selected_payment_option = option;
        };
        callbacks.dc_charge_loop_req = [this](const session::feedback::DcChargeLoopReq&) { ++setpoints_forwarded; };
        callbacks.ev_charge_parameters = [this](const session::feedback::EvChargeParameters& parameters) {
            ev_charge_parameters = parameters;
        };
        callbacks.dc_max_limits = [this](const session::feedback::DcMaximumLimits& limits) { dc_max_limits = limits; };
        callbacks.dc_ev_charge_progress = [this](const session::feedback::DcEvChargeProgress& progress) {
            charge_progress.push_back(progress);
        };
        return callbacks;
    }
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

            THEN("The machine moves to ContractAuthentication and reports the payment option") {
                REQUIRE(secc.fsm.state() == StateID::ContractAuthentication);
                const auto res = secc.fsm.response<message_din::ServicePaymentSelectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                // Always ExternalPayment in DIN 70121 [V2G-DC-395].
                REQUIRE(secc.selected_payment_option == shared_datatypes::PaymentOption::ExternalPayment);
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

SCENARIO("DIN SPEC 70121 SECC EV facts reported to the module") {

    Secc secc;

    GIVEN("A machine in ChargeParameterDiscovery") {
        to_charge_parameter_discovery(secc);

        WHEN("The EV sends its DC charge parameters") {
            auto req = charge_parameter_req();
            auto& params = req.dc_ev_charge_parameter.value();
            params.ev_maximum_power_limit = 80000.0;
            params.ev_energy_capacity = 64000.0;
            params.ev_energy_request = 32000.0;
            params.full_soc = 97;
            params.bulk_soc = 80;
            params.dc_ev_status.ev_ress_soc = 42;
            secc.drive(req);

            THEN("They are forwarded to the module, requested mode included") {
                REQUIRE(secc.ev_charge_parameters.has_value());
                REQUIRE(secc.ev_charge_parameters->requested_energy_transfer ==
                        shared_datatypes::EnergyTransferMode::DC_extended);
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
            }
            THEN("No AC parameters and no departure time are reported (DIN SPEC 70121 carries neither)") {
                REQUIRE_FALSE(secc.ev_charge_parameters->ac.has_value());
                REQUIRE_FALSE(secc.ev_charge_parameters->departure_time.has_value());
            }

            THEN("The same maxima reach the power supply") {
                REQUIRE(secc.dc_max_limits.has_value());
                REQUIRE(secc.dc_max_limits->voltage == 400.0f);
                REQUIRE(secc.dc_max_limits->current == 200.0f);
                REQUIRE(secc.dc_max_limits->power.value() == 80000.0f);
            }
        }

        WHEN("The EV omits the optional EVMaximumPowerLimit") {
            secc.drive(charge_parameter_req());

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

    GIVEN("A machine in CurrentDemand") {
        to_current_demand(secc);
        secc.charge_progress.clear();

        WHEN("The EV reports its remaining charging times") {
            auto req = current_demand_req();
            req.remaining_time_to_full_soc = 1800.0;
            req.remaining_time_to_bulk_soc = 600.0;
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

            AND_WHEN("The EV signals charging complete") {
                req.charging_complete = true;
                secc.drive(req);

                THEN("The change is reported") {
                    REQUIRE(secc.charge_progress.size() == 2);
                    REQUIRE(secc.charge_progress.back().charging_complete);
                }
            }
        }
    }

    GIVEN("A machine in PreCharge") {
        to_pre_charge(secc);

        WHEN("The module reports that no insulation monitoring device is fitted") {
            secc.fsm.context().reported_isolation_status = d20::IsolationStatus::NoImd;
            secc.drive(pre_charge_req());

            THEN("PreChargeRes reports Valid: DIN SPEC 70121 has no No_IMD isolation level") {
                const auto res = secc.fsm.response<message_din::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->dc_evse_status.evse_isolation_status.value() == dt::IsolationLevel::Valid);
            }
        }

        WHEN("The module reports an isolation warning") {
            secc.fsm.context().reported_isolation_status = d20::IsolationStatus::Warning;
            secc.drive(pre_charge_req());

            THEN("PreChargeRes carries it instead of the hardcoded Valid") {
                const auto res = secc.fsm.response<message_din::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->dc_evse_status.evse_isolation_status.value() == dt::IsolationLevel::Warning);
            }
        }
    }
}

SCENARIO("DIN SPEC 70121 SECC no-energy pause") {

    Secc secc;

    // IEC 61851-23:2023 CC.3.5.3: the charger has no energy for this session. DIN always stops before the
    // cable check (unlike ISO 15118-2, which can still pre-charge), so the SECC must not move on to it.
    GIVEN("A machine in ChargeParameterDiscovery with no energy available") {
        to_charge_parameter_discovery(secc);
        secc.fsm.context().session_config.no_energy_pause = d20::NoEnergyPauseMode::BeforeCableCheck;

        WHEN("The EV asks for charge parameters") {
            secc.drive(charge_parameter_req());

            THEN("The EV is told to stop and the machine does not move on to CableCheck") {
                REQUIRE(secc.fsm.state() == StateID::ChargeParameterDiscovery);
                const auto res = secc.fsm.response<message_din::ChargeParameterDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(res->dc_evse_charge_parameter->dc_evse_status.evse_notification ==
                        dt::EvseNotification::StopCharging);
            }

            AND_WHEN("The EV reacts with SessionStopReq") {
                secc.drive(message_din::SessionStopRequest{});

                THEN("The session is stopped cleanly") {
                    REQUIRE(secc.fsm.state() == StateID::SessionStop);
                    REQUIRE(secc.fsm.response<message_din::SessionStopResponse>().has_value());
                }
            }
        }
    }

    GIVEN("A machine in ChargeParameterDiscovery where the EV may ignore the pause") {
        to_charge_parameter_discovery(secc);
        secc.fsm.context().session_config.no_energy_pause = d20::NoEnergyPauseMode::AllowEvToIgnorePause;

        WHEN("The EV asks for charge parameters") {
            secc.drive(charge_parameter_req());

            THEN("The stop is signalled but the machine still moves on to CableCheck") {
                REQUIRE(secc.fsm.state() == StateID::CableCheck);
                const auto res = secc.fsm.response<message_din::ChargeParameterDiscoveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->dc_evse_charge_parameter->dc_evse_status.evse_notification ==
                        dt::EvseNotification::StopCharging);
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

// [V2G-DC-391]: a request whose SessionID does not match the one assigned in SessionSetup is answered
// with FAILED_UnknownSession and terminates the session -- before any side effect (feedback signal,
// power-supply setpoint, CP gate) fires for the unknown session.
SCENARIO("DIN SPEC 70121 SECC unknown-session rejection") {

    Secc secc;

    GIVEN("A machine in ContractAuthentication") {
        to_contract_authentication(secc);

        WHEN("A ContractAuthenticationReq with a mismatched SessionID arrives") {
            secc.drive_wrong_session(message_din::ContractAuthenticationRequest{});

            THEN("It is rejected without triggering the EIM authorization flow") {
                const auto res = secc.fsm.response<message_din::ContractAuthenticationResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_UnknownSession);
                REQUIRE(secc.fsm.context().session_stopped);
                REQUIRE_FALSE(secc.saw_signal(session::feedback::Signal::REQUIRE_AUTH_EIM));
            }
        }
    }

    GIVEN("A machine in CableCheck without a reported CP State C/D") {
        to_charge_parameter_discovery(secc);
        secc.drive(charge_parameter_req());
        REQUIRE(secc.fsm.state() == StateID::CableCheck);

        WHEN("A CableCheckReq with a mismatched SessionID arrives") {
            secc.drive_wrong_session(message_din::CableCheckRequest{});

            THEN("It is rejected immediately instead of being parked, without starting the isolation test") {
                const auto res = secc.fsm.response<message_din::CableCheckResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_UnknownSession);
                REQUIRE(secc.fsm.context().session_stopped);
                REQUIRE_FALSE(secc.saw_signal(session::feedback::Signal::START_CABLE_CHECK));
                REQUIRE_FALSE(secc.fsm.context().expect_cp_state_cd);
            }
        }
    }

    GIVEN("A machine in PreCharge") {
        to_pre_charge(secc);

        WHEN("A PreChargeReq with a mismatched SessionID arrives") {
            secc.drive_wrong_session(pre_charge_req());

            THEN("It is rejected without forwarding the EV target to the power supply") {
                const auto res = secc.fsm.response<message_din::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_UnknownSession);
                REQUIRE(secc.fsm.context().session_stopped);
                REQUIRE_FALSE(secc.saw_signal(session::feedback::Signal::PRE_CHARGE_STARTED));
                REQUIRE(secc.setpoints_forwarded == 0);
            }
        }
    }

    GIVEN("A machine in the CurrentDemand charge loop") {
        to_current_demand(secc);
        const auto setpoints_before = secc.setpoints_forwarded;

        WHEN("A CurrentDemandReq with a mismatched SessionID arrives") {
            secc.drive_wrong_session(current_demand_req());

            THEN("It is rejected without forwarding the EV setpoint to the power supply") {
                const auto res = secc.fsm.response<message_din::CurrentDemandResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_UnknownSession);
                REQUIRE(secc.fsm.context().session_stopped);
                REQUIRE_FALSE(secc.saw_signal(session::feedback::Signal::CHARGE_LOOP_STARTED));
                REQUIRE(secc.setpoints_forwarded == setpoints_before);
            }
        }
    }

    GIVEN("A machine after PowerDelivery(off) with the EV still in CP State C") {
        to_current_demand(secc);
        secc.drive(current_demand_req());
        secc.drive(power_delivery_req(false));
        REQUIRE(secc.fsm.state() == StateID::WeldingDetection);

        WHEN("A WeldingDetectionReq with a mismatched SessionID arrives") {
            secc.drive_wrong_session(message_din::WeldingDetectionRequest{});

            THEN("It is rejected immediately instead of being parked behind the CP State B gate") {
                const auto res = secc.fsm.response<message_din::WeldingDetectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_UnknownSession);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }

        WHEN("A SessionStopReq with a mismatched SessionID arrives") {
            secc.drive_wrong_session(message_din::SessionStopRequest{});

            THEN("It is rejected immediately instead of being parked behind the CP State B gate") {
                REQUIRE(secc.fsm.state() == StateID::SessionStop);
                const auto res = secc.fsm.response<message_din::SessionStopResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED_UnknownSession);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }
    }
}

SCENARIO("DIN SPEC 70121 SECC charger stop and shutdown paths") {

    Secc secc;

    GIVEN("A machine in PreCharge") {
        to_pre_charge(secc);

        // The engine latches a StopCharging control event on the context in ANY state (EvseV2G parity);
        // the FSM harness sets the flag the way the engine does.
        WHEN("The module requests a stop during pre-charge") {
            secc.fsm.context().charger_stop_requested = true;
            secc.drive(pre_charge_req());

            THEN("The stop is signalled via EVSE_Shutdown already in the PreChargeRes") {
                const auto res = secc.fsm.response<message_din::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Shutdown);
                // [V2G-DC-500]: for DC the EVSENotification stays None.
                REQUIRE(res->dc_evse_status.evse_notification == dt::EvseNotification::None);
            }

            AND_WHEN("The EV ignores it and enters the charge loop") {
                secc.drive(power_delivery_req(true));
                secc.drive(current_demand_req());

                THEN("The latched stop is still signalled via EVSE_Shutdown in the CurrentDemandRes") {
                    const auto res = secc.fsm.response<message_din::CurrentDemandResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Shutdown);
                    REQUIRE(res->dc_evse_status.evse_notification == dt::EvseNotification::None);
                }
            }
        }

        WHEN("The EV keeps the session going beyond the stop-charging guard") {
            secc.fsm.context().charger_stop_requested = true;
            secc.fsm.context().charger_stop_ignored = true; // set by the engine on the STOP_CHARGING timeout
            secc.drive(pre_charge_req());

            THEN("The stop is enforced: the response is FAILED and the session terminates") {
                const auto res = secc.fsm.response<message_din::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }

        // The SECC answers FAILED and terminates with that response ([V2G-DC-866] uses the same shape for
        // its own abort), rather than dropping the TCP connection and leaving the EV without a reason.
        // The engine sets both flags on the EmergencyShutdown control event.
        WHEN("The module reports an emergency shutdown") {
            secc.fsm.context().active_error = d20::EvseErrorCode::EmergencyShutdown;
            secc.fsm.context().emergency_shutdown = true;
            secc.drive(pre_charge_req());

            THEN("The next response is FAILED, carries EVSE_EmergencyShutdown, and ends the session") {
                const auto res = secc.fsm.response<message_din::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::FAILED);
                REQUIRE(res->dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_EmergencyShutdown);
                REQUIRE(secc.fsm.context().session_stopped);
            }
        }

        // [V2G-DC-637]: every other status code is informational, so a malfunction is reported but does
        // not end the session on its own -- the EV decides (EvseV2G terminates only on an emergency too).
        WHEN("The module reports a malfunction") {
            secc.fsm.context().active_error = d20::EvseErrorCode::Malfunction;
            secc.drive(pre_charge_req());

            THEN("The response reports EVSE_Malfunction but stays OK and the session continues") {
                const auto res = secc.fsm.response<message_din::PreChargeResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(res->dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Malfunction);
                REQUIRE_FALSE(secc.fsm.context().session_stopped);
            }

            AND_WHEN("The EV then asks to start power delivery") {
                secc.drive(power_delivery_req(true));

                THEN("FAILED_PowerDeliveryNotApplied [V2G-DC-401]") {
                    const auto res = secc.fsm.response<message_din::PowerDeliveryResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::FAILED_PowerDeliveryNotApplied);
                }
            }
        }
    }

    GIVEN("A machine in the CurrentDemand charge loop") {
        to_current_demand(secc);

        WHEN("A graceful HLC shutdown is requested") {
            secc.fsm.context().request_shutdown();
            secc.drive(current_demand_req());

            THEN("It is signalled via EVSE_Shutdown like an EVSE-initiated stop") {
                const auto res = secc.fsm.response<message_din::CurrentDemandResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Shutdown);
            }
        }

        WHEN("The EV repeats an identical setpoint and then changes it") {
            secc.drive(current_demand_req());
            const auto after_first = secc.setpoints_forwarded;
            secc.drive(current_demand_req());
            const auto after_repeat = secc.setpoints_forwarded;

            auto changed = current_demand_req();
            changed.ev_target_current = 150.0;
            secc.drive(changed);

            THEN("The setpoint is forwarded to the power supply only on change (EvseV2G parity)") {
                REQUIRE(after_repeat == after_first);
                REQUIRE(secc.setpoints_forwarded == after_first + 1);
            }
        }
    }
}
