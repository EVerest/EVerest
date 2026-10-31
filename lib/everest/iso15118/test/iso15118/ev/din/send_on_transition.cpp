// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <vector>

#include "helper.hpp"

#include <iso15118/ev/din/engine.hpp>
#include <iso15118/ev/din/state/cable_check.hpp>
#include <iso15118/ev/din/state/charge_parameter_discovery.hpp>
#include <iso15118/ev/din/state/contract_authentication.hpp>
#include <iso15118/ev/din/state/current_demand.hpp>
#include <iso15118/ev/din/state/power_delivery.hpp>
#include <iso15118/ev/din/state/pre_charge.hpp>
#include <iso15118/ev/din/state/service_discovery.hpp>
#include <iso15118/ev/din/state/service_payment_selection.hpp>
#include <iso15118/ev/din/state/session_setup.hpp>
#include <iso15118/ev/din/state/welding_detection.hpp>
#include <iso15118/ev/session/latches.hpp>
#include <iso15118/io/stream_view.hpp>
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
#include <iso15118/message_din/type.hpp>
#include <iso15118/message_din/welding_detection.hpp>

using namespace iso15118;

namespace {

namespace dt = message_din::datatypes;
namespace state = ev::din::state;
using ev::din::Disposition;
using ev::din::Event;
using ev::din::StateID;

constexpr uint16_t CHARGE_SERVICE_ID = 7;

template <typename Res> Res ok() {
    Res res{};
    res.header = header();
    res.response_code = dt::ResponseCode::OK;
    return res;
}

message_din::SessionSetupResponse session_setup_new() {
    auto res = ok<message_din::SessionSetupResponse>();
    res.response_code = dt::ResponseCode::OK_NewSessionEstablished;
    res.evse_id = dt::EvseId{0x2A};
    return res;
}

message_din::ServiceDiscoveryResponse service_discovery_with(bool eim) {
    auto res = ok<message_din::ServiceDiscoveryResponse>();
    if (eim) {
        res.payment_options.push_back(dt::PaymentOption::ExternalPayment);
    }
    res.charge_service.service_tag.service_id = CHARGE_SERVICE_ID;
    res.charge_service.service_tag.service_category = dt::ServiceCategory::EVCharging;
    res.charge_service.energy_transfer_type = dt::SupportedEnergyTransferMode::DC_extended;
    return res;
}

message_din::ContractAuthenticationResponse contract_authentication_finished() {
    auto res = ok<message_din::ContractAuthenticationResponse>();
    res.evse_processing = dt::EvseProcessing::Finished;
    return res;
}

message_din::ChargeParameterDiscoveryResponse
charge_parameter_discovery_finished(dt::EvseNotification notification = dt::EvseNotification::None) {
    auto res = ok<message_din::ChargeParameterDiscoveryResponse>();
    res.evse_processing = dt::EvseProcessing::Finished;
    dt::DcEvseChargeParameter param{};
    param.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    param.dc_evse_status.evse_notification = notification;
    param.evse_maximum_current_limit = 200.0;
    param.evse_maximum_voltage_limit = 920.0;
    param.evse_minimum_voltage_limit = 150.0;
    param.evse_peak_current_ripple = 1.0;
    res.dc_evse_charge_parameter = param;
    return res;
}

message_din::CableCheckResponse cable_check_finished(dt::DcEvseStatusCode status = dt::DcEvseStatusCode::EVSE_Ready) {
    auto res = ok<message_din::CableCheckResponse>();
    res.evse_processing = dt::EvseProcessing::Finished;
    res.dc_evse_status.evse_status_code = status;
    res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
    return res;
}

message_din::PreChargeResponse pre_charge_converged() {
    auto res = ok<message_din::PreChargeResponse>();
    res.evse_present_voltage = 400.0;
    return res;
}

message_din::CurrentDemandResponse current_demand_with(dt::EvseNotification notification) {
    auto res = ok<message_din::CurrentDemandResponse>();
    res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    res.dc_evse_status.evse_notification = notification;
    return res;
}

message_din::WeldingDetectionResponse welding_safe() {
    auto res = ok<message_din::WeldingDetectionResponse>();
    res.evse_present_voltage = 0.0;
    return res;
}

const auto seed_cp_c = [](DinStateHelper& helper) { helper.get_context().set_cp_state(true); };

template <typename Res> ev::session::FeedOutcome<StateID> respond(ev::din::Engine& engine, const Res& res) {
    REQUIRE(engine.take_request().has_value());
    std::vector<uint8_t> buffer(2048);
    const auto size = message_din::serialize(res, io::StreamOutputView{buffer.data(), buffer.size()});
    REQUIRE(engine.stage_response(v2gtp::PayloadType::SAP, io::StreamInputView{buffer.data(), size}));
    return engine.feed(Event::V2GTP_MESSAGE);
}

} // namespace

SCENARIO("DIN 70121 EV engine start sends SessionSetupReq ahead of the state") {
    ev::session::Latches latches;
    everest::lib::util::monitor<ev::params::DcChargeParams> dc_params{default_dc_params()};
    ev::din::Engine engine{{}, ev::params::EvSessionParams{}, latches, dc_params, false, std::nullopt, {}};
    engine.start();

    REQUIRE(engine.current_state() == StateID::SessionSetup);
    const auto taken = engine.take_request();
    REQUIRE(taken.has_value());
    const message_din::Variant variant{io::StreamInputView{taken->first.data(), taken->first.size()}};
    const auto* req = variant.get_if<message_din::SessionSetupRequest>();
    REQUIRE(req != nullptr);
    const auto& mac = ev::params::EvSessionParams{}.evcc_mac;
    REQUIRE(req->evcc_id == dt::EvccId(mac.begin(), mac.end()));
    REQUIRE(req->header.session_id == dt::SessionId{});
    REQUIRE_FALSE(engine.has_request());
}

SCENARIO("DIN 70121 EV SessionSetup sends nothing on enter") {
    DinStateHelper helper{{}};
    auto session_setup = helper.get_context().create_state<state::SessionSetup>();
    session_setup->enter();
    REQUIRE_FALSE(helper.get_message_exchange().has_request());
}

SCENARIO("DIN 70121 EV engine holds CableCheck until CP reaches state C or D [V2G-DC-547]") {
    ev::session::Latches latches;
    everest::lib::util::monitor<ev::params::DcChargeParams> dc_params{default_dc_params()};
    ev::din::Engine engine{{}, ev::params::EvSessionParams{}, latches, dc_params, true, std::nullopt, {}};
    engine.start();

    REQUIRE(respond(engine, session_setup_new()).violation == std::nullopt);
    REQUIRE(respond(engine, service_discovery_with(true)).violation == std::nullopt);
    REQUIRE(respond(engine, ok<message_din::ServicePaymentSelectionResponse>()).violation == std::nullopt);
    REQUIRE(respond(engine, contract_authentication_finished()).violation == std::nullopt);
    REQUIRE(engine.current_state() == StateID::ChargeParameterDiscovery);

    const auto held = respond(engine, charge_parameter_discovery_finished());
    REQUIRE(held.output == Disposition::Waiting);
    REQUIRE(held.violation == std::nullopt);
    REQUIRE(engine.current_state() == StateID::CableCheck);
    REQUIRE_FALSE(engine.has_request());
    REQUIRE_FALSE(engine.is_stopped());

    WHEN("CP reaches state C") {
        ev::session::latch(latches, ev::din::CpState{true});
        const auto released = engine.feed(Event::CONTROL_MESSAGE);

        THEN("exactly one CableCheckReq goes out") {
            REQUIRE(released.output == Disposition::Awaiting);
            REQUIRE(released.violation == std::nullopt);
            const auto taken = engine.take_request();
            REQUIRE(taken.has_value());
            const message_din::Variant variant{io::StreamInputView{taken->first.data(), taken->first.size()}};
            REQUIRE(variant.get_if<message_din::CableCheckRequest>() != nullptr);
            REQUIRE_FALSE(engine.has_request());
        }
    }

    WHEN("the EV pauses before CP reaches state C") {
        ev::session::latch(latches, ev::din::PauseCharging{true});
        const auto stopped = engine.feed(Event::CONTROL_MESSAGE);

        THEN("SessionStopReq goes out instead of a CableCheckReq") {
            REQUIRE(stopped.output == Disposition::Transitioning);
            REQUIRE(stopped.violation == std::nullopt);
            REQUIRE(engine.current_state() == StateID::SessionStop);
            const auto taken = engine.take_request();
            REQUIRE(taken.has_value());
            const message_din::Variant variant{io::StreamInputView{taken->first.data(), taken->first.size()}};
            REQUIRE(variant.get_if<message_din::SessionStopRequest>() != nullptr);
        }
    }
}

SCENARIO("DIN 70121 EV session setup transitions send the successor's first request") {
    GIVEN("SessionSetup establishing a new session") {
        auto pred = DinPredecessor::of<state::SessionSetup>();
        pred.ctx.set_session_id(dt::SessionId{});
        const auto sent = pred.transitions_to(StateID::ServiceDiscovery, session_setup_new());
        const auto req = sent.get<message_din::ServiceDiscoveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->header.session_id == SESSION_ID);
    }

    GIVEN("ServiceDiscovery offering ExternalPayment") {
        auto pred = DinPredecessor::of<state::ServiceDiscovery>();
        const auto sent = pred.transitions_to(StateID::ServicePaymentSelection, service_discovery_with(true));
        const auto req = sent.get<message_din::ServicePaymentSelectionRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->selected_payment_option == dt::PaymentOption::ExternalPayment);
        REQUIRE(req->selected_service_list.size() == 1);
        REQUIRE(req->selected_service_list.front().service_id == CHARGE_SERVICE_ID);
    }

    GIVEN("ServiceDiscovery without ExternalPayment") {
        auto pred = DinPredecessor::of<state::ServiceDiscovery>();
        const auto sent = pred.transitions_to(StateID::SessionStop, service_discovery_with(false));
        REQUIRE(sent.get<message_din::SessionStopRequest>().has_value());
    }

    GIVEN("a pause latched before PowerDelivery(Start)") {
        auto pred = DinPredecessor::of<state::ServiceDiscovery>();
        pred.ctx.set_pause_charging_requested(true);
        const auto sent = pred.transitions_to(StateID::SessionStop, service_discovery_with(true));
        REQUIRE(sent.get<message_din::SessionStopRequest>().has_value());
    }

    GIVEN("ServicePaymentSelection accepted") {
        auto pred = DinPredecessor::make<state::ServicePaymentSelection>(ev::params::EvSessionParams{}, false, no_seed);
        const auto sent =
            pred.transitions_to(StateID::ContractAuthentication, ok<message_din::ServicePaymentSelectionResponse>());
        REQUIRE(sent.get<message_din::ContractAuthenticationRequest>().has_value());
    }

    GIVEN("ContractAuthentication finished") {
        auto pred = DinPredecessor::of<state::ContractAuthentication>();
        const auto sent = pred.transitions_to(StateID::ChargeParameterDiscovery, contract_authentication_finished());
        const auto req = sent.get<message_din::ChargeParameterDiscoveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->dc_ev_charge_parameter.has_value());
        REQUIRE_FALSE(req->dc_ev_charge_parameter->dc_ev_status.ev_ready);
        REQUIRE(req->dc_ev_charge_parameter->ev_maximum_voltage_limit == 900.0);
    }
}

SCENARIO("DIN 70121 EV DC transitions send the successor's first request") {
    GIVEN("ChargeParameterDiscovery finished without CP-state feedback") {
        auto pred = DinPredecessor::of<state::ChargeParameterDiscovery>();
        const auto sent = pred.transitions_to(StateID::CableCheck, charge_parameter_discovery_finished());
        const auto req = sent.get<message_din::CableCheckRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->dc_ev_status.ev_ready);
    }

    GIVEN("ChargeParameterDiscovery finished with CP already in state C or D") {
        auto pred =
            DinPredecessor::make<state::ChargeParameterDiscovery>(ev::params::EvSessionParams{}, true, seed_cp_c);
        const auto sent = pred.transitions_to(StateID::CableCheck, charge_parameter_discovery_finished());
        REQUIRE(sent.get<message_din::CableCheckRequest>().has_value());
    }

    GIVEN("ChargeParameterDiscovery finished before CP reached state C or D") {
        auto pred = DinPredecessor::make<state::ChargeParameterDiscovery>(ev::params::EvSessionParams{}, true, no_seed);
        pred.waits_in(StateID::CableCheck, charge_parameter_discovery_finished());

        WHEN("CP reaches state C") {
            pred.ctx.set_cp_state(true);
            const auto result = pred.control(ev::din::CpState{true});

            THEN("the held CableCheckReq goes out") {
                REQUIRE(result.output == Disposition::Awaiting);
                const auto sent = take_all_requests(pred.helper->get_message_exchange());
                REQUIRE(sent.get<message_din::CableCheckRequest>().has_value());
            }
        }

        WHEN("the EV stops before CP reaches state C") {
            pred.ctx.set_stop_charging_requested(true);
            const auto result = pred.control(ev::din::StopCharging{true});

            THEN("SessionStopReq goes out on the transition and no CableCheckReq") {
                REQUIRE(result.output == Disposition::Transitioning);
                REQUIRE(result.new_state->get_id() == StateID::SessionStop);
                const auto sent = take_all_requests(pred.helper->get_message_exchange());
                REQUIRE(sent.get<message_din::SessionStopRequest>().has_value());
                REQUIRE_FALSE(sent.get<message_din::CableCheckRequest>().has_value());
            }
        }
    }

    GIVEN("ChargeParameterDiscovery finished with the SECC stopping") {
        auto pred = DinPredecessor::of<state::ChargeParameterDiscovery>();
        const auto sent = pred.transitions_to(StateID::SessionStop,
                                              charge_parameter_discovery_finished(dt::EvseNotification::StopCharging));
        REQUIRE(sent.get<message_din::SessionStopRequest>().has_value());
    }

    GIVEN("CableCheck finished") {
        auto pred = DinPredecessor::of<state::ChargeParameterDiscovery>();
        pred.transitions_to(StateID::CableCheck, charge_parameter_discovery_finished());
        const auto sent = pred.transitions_to(StateID::PreCharge, cable_check_finished());
        const auto req = sent.get<message_din::PreChargeRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->dc_ev_status.ev_ready);
        REQUIRE(req->ev_target_voltage == 400.0);
        REQUIRE(req->ev_target_current == 0.0);
    }

    GIVEN("CableCheck finished with the EVSE shutting down") {
        auto pred = DinPredecessor::of<state::ChargeParameterDiscovery>();
        pred.transitions_to(StateID::CableCheck, charge_parameter_discovery_finished());
        const auto sent =
            pred.transitions_to(StateID::SessionStop, cable_check_finished(dt::DcEvseStatusCode::EVSE_Shutdown));
        REQUIRE(sent.get<message_din::SessionStopRequest>().has_value());
    }

    GIVEN("PreCharge converged") {
        auto pred = DinPredecessor::of<state::PreCharge>();
        const auto sent = pred.transitions_to(StateID::PowerDelivery, pre_charge_converged());
        const auto req = sent.get<message_din::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->ready_to_charge_state);
        REQUIRE(req->dc_ev_power_delivery_parameter->dc_ev_status.ev_ready);
    }

    GIVEN("PowerDelivery(Start) accepted") {
        auto pred = DinPredecessor::of<state::PowerDelivery>(state::PowerDelivery::Start{});
        const auto sent = pred.transitions_to(StateID::CurrentDemand, ok<message_din::PowerDeliveryResponse>());
        const auto req = sent.get<message_din::CurrentDemandRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->ev_target_current == 100.0);
    }

    GIVEN("CurrentDemand with the SECC stopping") {
        auto pred = DinPredecessor::of<state::CurrentDemand>();
        const auto sent =
            pred.transitions_to(StateID::PowerDelivery, current_demand_with(dt::EvseNotification::StopCharging));
        const auto req = sent.get<message_din::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE_FALSE(req->ready_to_charge_state);
        REQUIRE_FALSE(req->dc_ev_power_delivery_parameter->dc_ev_status.ev_ready);
    }

    GIVEN("CurrentDemand with the EV pausing") {
        auto pred = DinPredecessor::of<state::CurrentDemand>();
        pred.ctx.set_pause_charging_requested(true);
        const auto sent = pred.transitions_to(StateID::PowerDelivery, current_demand_with(dt::EvseNotification::None));
        const auto req = sent.get<message_din::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE_FALSE(req->ready_to_charge_state);
    }

    GIVEN("PowerDelivery(Stop) accepted") {
        auto pred =
            DinPredecessor::of<state::PowerDelivery>(state::PowerDelivery::Stop{ev::session::StopReason::EvPause});
        const auto sent = pred.transitions_to(StateID::WeldingDetection, ok<message_din::PowerDeliveryResponse>());
        const auto req = sent.get<message_din::WeldingDetectionRequest>();
        REQUIRE(req.has_value());
        REQUIRE_FALSE(req->dc_ev_status.ev_ready);
    }

    GIVEN("WeldingDetection at a safe voltage") {
        auto pred = DinPredecessor::of<state::WeldingDetection>(ev::session::StopReason::EvStop);
        const auto sent = pred.transitions_to(StateID::SessionStop, welding_safe());
        REQUIRE(sent.get<message_din::SessionStopRequest>().has_value());
    }

    GIVEN("WeldingDetection running out of time between polls") {
        const auto poll_pending = [](DinStateHelper& helper) {
            helper.get_context().send_request(message_din::WeldingDetectionRequest{});
        };
        auto pred = DinPredecessor::make<state::WeldingDetection>(ev::params::EvSessionParams{}, false, poll_pending,
                                                                  ev::session::StopReason::EvStop);
        const auto sent = pred.transitions_on(StateID::SessionStop, Event::WELDING_TIMEOUT);
        REQUIRE(sent.get<message_din::SessionStopRequest>().has_value());
    }
}
