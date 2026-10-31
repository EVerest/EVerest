// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <vector>

#include "helper.hpp"
#include <iso15118/io/logging.hpp>

#include <iso15118/ev/d2/engine.hpp>
#include <iso15118/ev/d2/state/authorization.hpp>
#include <iso15118/ev/d2/state/cable_check.hpp>
#include <iso15118/ev/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/ev/d2/state/charging_status.hpp>
#include <iso15118/ev/d2/state/current_demand.hpp>
#include <iso15118/ev/d2/state/metering_receipt.hpp>
#include <iso15118/ev/d2/state/payment_details.hpp>
#include <iso15118/ev/d2/state/payment_service_selection.hpp>
#include <iso15118/ev/d2/state/power_delivery.hpp>
#include <iso15118/ev/d2/state/pre_charge.hpp>
#include <iso15118/ev/d2/state/service_discovery.hpp>
#include <iso15118/ev/d2/state/session_setup.hpp>
#include <iso15118/ev/d2/state/welding_detection.hpp>
#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/session/latches.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/cable_check.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/charging_status.hpp>
#include <iso15118/message_2/current_demand.hpp>
#include <iso15118/message_2/metering_receipt.hpp>
#include <iso15118/message_2/payment_details.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/power_delivery.hpp>
#include <iso15118/message_2/pre_charge.hpp>
#include <iso15118/message_2/service_discovery.hpp>
#include <iso15118/message_2/session_setup.hpp>
#include <iso15118/message_2/session_stop.hpp>
#include <iso15118/message_2/type.hpp>
#include <iso15118/message_2/welding_detection.hpp>

using namespace iso15118;

namespace {

namespace dt = message_2::datatypes;
namespace state = ev::d2::state;
using dt::ResponseCode;
using ev::d2::Disposition;
using ev::d2::Event;
using ev::d2::StateID;

message_2::ServiceDiscoveryResponse service_discovery_ok(bool eim, bool contract) {
    auto res = d2_ok<message_2::ServiceDiscoveryResponse>();
    if (eim) {
        res.payment_option_list.push_back(dt::PaymentOption::ExternalPayment);
    }
    if (contract) {
        res.payment_option_list.push_back(dt::PaymentOption::Contract);
        dt::Service service;
        service.service_id = dt::CERTIFICATE_SERVICE_ID;
        service.service_category = dt::ServiceCategory::ContractCertificate;
        service.free_service = true;
        res.service_list.emplace().push_back(service);
    }
    res.charge_service.service_id = dt::CHARGE_SERVICE_ID;
    res.charge_service.service_category = dt::ServiceCategory::EVCharging;
    res.charge_service.supported_energy_transfer_mode.push_back(dt::EnergyTransferMode::DC_extended);
    return res;
}

message_2::AuthorizationResponse authorization_finished() {
    auto res = d2_ok<message_2::AuthorizationResponse>();
    res.evse_processing = dt::EVSEProcessing::Finished;
    return res;
}

message_2::ChargeParameterDiscoveryResponse charge_parameter_discovery_finished() {
    auto res = d2_ok<message_2::ChargeParameterDiscoveryResponse>();
    res.evse_processing = dt::EVSEProcessing::Finished;
    dt::SAScheduleTuple tuple{};
    tuple.sa_schedule_tuple_id = 7;
    res.sa_schedule_list.emplace().push_back(tuple);
    return res;
}

message_2::CableCheckResponse cable_check_finished() {
    auto res = d2_ok<message_2::CableCheckResponse>();
    res.evse_processing = dt::EVSEProcessing::Finished;
    res.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;
    res.dc_evse_status.isolation_status = dt::IsolationLevel::Valid;
    return res;
}

message_2::PreChargeResponse pre_charge_converged() {
    auto res = d2_ok<message_2::PreChargeResponse>();
    res.evse_present_voltage = dt::to_physical_value(400.0f, dt::Unit::V);
    return res;
}

message_2::CurrentDemandResponse current_demand_with(dt::EVSENotification notification, bool receipt_required) {
    auto res = d2_ok<message_2::CurrentDemandResponse>();
    res.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;
    res.dc_evse_status.notification = notification;
    res.sa_schedule_tuple_id = 7;
    if (receipt_required) {
        res.receipt_required = true;
        dt::MeterInfo meter;
        meter.meter_id = "METER-1";
        meter.meter_reading = 1234;
        res.meter_info = meter;
    }
    return res;
}

message_2::WeldingDetectionResponse welding_safe() {
    auto res = d2_ok<message_2::WeldingDetectionResponse>();
    res.evse_present_voltage = dt::to_physical_value(0.0f, dt::Unit::V);
    return res;
}

ev::params::EvSessionParams pnc_params() {
    ev::params::EvSessionParams params;
    params.secure_transport = true;
    params.pnc.prefer_contract = true;
    params.pnc.contract =
        ev::d2::ContractMaterial{{0x30, 0x01}, {}, make_test_ec_key_pem(), std::nullopt, "DEPNXCONTRACT1"};
    return params;
}

const auto seed_charge_service = [](D2StateHelper& helper) {
    helper.get_context().evse_info.selected_charge_service_id = dt::CHARGE_SERVICE_ID;
};

const auto seed_contract = [](D2StateHelper& helper) {
    auto& ctx = helper.get_context();
    ctx.pnc.contract_selected = true;
    ctx.pnc.contract.emplace().key_pem = make_test_ec_key_pem();
    ctx.pnc.gen_challenge = dt::GenChallenge{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
};

} // namespace

SCENARIO("ISO15118-2 EV engine start sends SessionSetupReq ahead of the state") {
    ev::session::Latches latches;
    everest::lib::util::monitor<ev::params::DcChargeParams> dc_params{default_dc_params()};
    ev::d2::Engine engine{{}, ev::params::EvSessionParams{}, latches, dc_params, false, std::nullopt, {}};
    engine.start();

    REQUIRE(engine.current_state() == StateID::SessionSetup);
    const auto taken = engine.take_request();
    REQUIRE(taken.has_value());
    const message_2::Variant variant{io::StreamInputView{taken->first.data(), taken->first.size()}};
    const auto* req = variant.get_if<message_2::SessionSetupRequest>();
    REQUIRE(req != nullptr);
    REQUIRE(req->evcc_id == ev::params::EvSessionParams{}.evcc_mac);
    REQUIRE(req->header.session_id == dt::SessionId{});
    REQUIRE_FALSE(engine.has_request());
}

template <typename Res> ev::session::FeedOutcome<StateID> respond(ev::d2::Engine& engine, const Res& res) {
    REQUIRE(engine.take_request().has_value());
    std::vector<uint8_t> buffer(2048);
    const auto size = message_2::serialize(res, io::StreamOutputView{buffer.data(), buffer.size()});
    REQUIRE(engine.stage_response(v2gtp::PayloadType::SAP, io::StreamInputView{buffer.data(), size}));
    return engine.feed(Event::V2GTP_MESSAGE);
}

SCENARIO("ISO15118-2 EV engine holds CableCheck until CP reaches state C or D") {
    ev::session::Latches latches;
    everest::lib::util::monitor<ev::params::DcChargeParams> dc_params{default_dc_params()};
    ev::d2::Engine engine{{}, ev::params::EvSessionParams{}, latches, dc_params, true, std::nullopt, {}};
    engine.start();

    auto session_setup = d2_ok<message_2::SessionSetupResponse>();
    session_setup.response_code = ResponseCode::OK_NewSessionEstablished;
    REQUIRE(respond(engine, session_setup).violation == std::nullopt);
    REQUIRE(respond(engine, service_discovery_ok(true, false)).violation == std::nullopt);
    REQUIRE(respond(engine, d2_ok<message_2::PaymentServiceSelectionResponse>()).violation == std::nullopt);
    REQUIRE(respond(engine, authorization_finished()).violation == std::nullopt);
    REQUIRE(engine.current_state() == StateID::ChargeParameterDiscovery);

    auto charge_parameter_discovery = charge_parameter_discovery_finished();
    dt::PMaxScheduleEntry entry;
    entry.p_max = dt::to_physical_value(60000.0f, dt::Unit::W);
    charge_parameter_discovery.sa_schedule_list->front().pmax_schedule.push_back(entry);
    dt::DC_EVSEChargeParameter evse_param{};
    evse_param.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;
    evse_param.evse_maximum_voltage_limit = dt::to_physical_value(900.0f, dt::Unit::V);
    evse_param.evse_maximum_current_limit = dt::to_physical_value(150.0f, dt::Unit::A);
    evse_param.evse_minimum_voltage_limit = dt::to_physical_value(150.0f, dt::Unit::V);
    evse_param.evse_minimum_current_limit = dt::to_physical_value(0.0f, dt::Unit::A);
    evse_param.evse_peak_current_ripple = dt::to_physical_value(1.0f, dt::Unit::A);
    charge_parameter_discovery.dc_evse_charge_parameter = evse_param;
    const auto held = respond(engine, charge_parameter_discovery);
    REQUIRE(held.output == Disposition::Waiting);
    REQUIRE(held.violation == std::nullopt);
    REQUIRE(engine.current_state() == StateID::CableCheck);
    REQUIRE_FALSE(engine.has_request());
    REQUIRE_FALSE(engine.is_stopped());

    WHEN("CP reaches state C") {
        ev::session::latch(latches, ev::d20::CpState{true});
        const auto released = engine.feed(Event::CONTROL_MESSAGE);

        THEN("exactly one CableCheckReq goes out") {
            REQUIRE(released.output == Disposition::Awaiting);
            REQUIRE(released.violation == std::nullopt);
            const auto taken = engine.take_request();
            REQUIRE(taken.has_value());
            const message_2::Variant variant{io::StreamInputView{taken->first.data(), taken->first.size()}};
            REQUIRE(variant.get_if<message_2::CableCheckRequest>() != nullptr);
            REQUIRE_FALSE(engine.has_request());
        }

        AND_WHEN("a second CP event arrives before the response") {
            REQUIRE(engine.take_request().has_value());
            ev::session::latch(latches, ev::d20::CpState{true});
            const auto again = engine.feed(Event::CONTROL_MESSAGE);

            THEN("it is ignored and nothing more goes out") {
                REQUIRE(again.output == Disposition::Ignored);
                REQUIRE(again.violation == std::nullopt);
                REQUIRE_FALSE(engine.has_request());
                REQUIRE(engine.current_state() == StateID::CableCheck);
            }
        }
    }
}

SCENARIO("ISO15118-2 EV SessionSetup sends nothing on enter") {
    D2StateHelper helper{{}};
    auto session_setup = helper.get_context().create_state<state::SessionSetup>();
    session_setup->enter();
    REQUIRE_FALSE(helper.get_message_exchange().has_request());
}

SCENARIO("ISO15118-2 EV session setup transitions send the successor's first request") {
    GIVEN("SessionSetup establishing a new session") {
        auto pred = D2Predecessor::of<state::SessionSetup>();
        pred.ctx.set_session_id(dt::SessionId{});
        auto res = d2_ok<message_2::SessionSetupResponse>();
        res.response_code = ResponseCode::OK_NewSessionEstablished;
        const auto sent = pred.transitions_to(StateID::ServiceDiscovery, res);
        const auto req = sent.get<message_2::ServiceDiscoveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->header.session_id == D2_SESSION_ID);
    }

    GIVEN("ServiceDiscovery offering ExternalPayment") {
        auto pred = D2Predecessor::of<state::ServiceDiscovery>();
        const auto sent = pred.transitions_to(StateID::PaymentServiceSelection, service_discovery_ok(true, false));
        const auto req = sent.get<message_2::PaymentServiceSelectionRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->selected_payment_option == dt::PaymentOption::ExternalPayment);
        REQUIRE(req->selected_service_list.size() == 1);
        REQUIRE(req->selected_service_list.front().service_id == dt::CHARGE_SERVICE_ID);
        REQUIRE_FALSE(pred.ctx.pnc.contract_selected);
    }

    GIVEN("ServiceDiscovery offering Contract and the Certificate service to an EV without a contract") {
        ev::params::EvSessionParams params;
        params.secure_transport = true;
        params.pnc.prefer_contract = true;
        auto pred = D2Predecessor::make<state::ServiceDiscovery>(params, false, d2_no_seed);
        const auto sent = pred.transitions_to(StateID::ServiceDetail, service_discovery_ok(true, true));
        REQUIRE(sent.types() == std::vector<message_2::Type>{message_2::Type::ServiceDetailReq});
    }

    GIVEN("ServiceDiscovery offering no usable payment option") {
        auto pred = D2Predecessor::of<state::ServiceDiscovery>();
        const auto sent = pred.transitions_to(StateID::SessionStop, service_discovery_ok(false, true));
        const auto req = sent.get<message_2::SessionStopRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charging_session == dt::ChargingSession::Terminate);
    }

    GIVEN("a stop latched before PowerDelivery(Start)") {
        auto pred = D2Predecessor::of<state::ServiceDiscovery>();
        pred.ctx.set_pause_charging_requested(true);
        const auto sent = pred.transitions_to(StateID::SessionStop, service_discovery_ok(true, false));
        const auto req = sent.get<message_2::SessionStopRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charging_session == dt::ChargingSession::Pause);
    }
}

SCENARIO("ISO15118-2 EV payment transitions send the successor's first request") {
    GIVEN("PaymentServiceSelection with ExternalPayment") {
        auto pred = D2Predecessor::make<state::PaymentServiceSelection>(ev::params::EvSessionParams{}, false,
                                                                        seed_charge_service);
        const auto sent =
            pred.transitions_to(StateID::Authorization, d2_ok<message_2::PaymentServiceSelectionResponse>());
        const auto req = sent.get<message_2::AuthorizationRequest>();
        REQUIRE(req.has_value());
        REQUIRE_FALSE(req->id.has_value());
        REQUIRE_FALSE(req->gen_challenge.has_value());
    }

    GIVEN("PaymentServiceSelection with a preloaded contract") {
        const auto seed = [](D2StateHelper& helper) { helper.get_context().pnc.contract_selected = true; };
        auto pred = D2Predecessor::make<state::PaymentServiceSelection>(pnc_params(), false, seed);
        const auto sent =
            pred.transitions_to(StateID::PaymentDetails, d2_ok<message_2::PaymentServiceSelectionResponse>());
        const auto req = sent.get<message_2::PaymentDetailsRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->emaid == "DEPNXCONTRACT1");
        REQUIRE(req->contract_certificate == std::vector<uint8_t>{0x30, 0x01});
    }

    GIVEN("PaymentDetails on a contract session") {
        auto pred = D2Predecessor::make<state::PaymentDetails>(ev::params::EvSessionParams{}, false, seed_contract);
        auto res = d2_ok<message_2::PaymentDetailsResponse>();
        res.gen_challenge = dt::GenChallenge{9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
        const auto sent = pred.transitions_to(StateID::Authorization, res);
        const auto req = sent.get<message_2::AuthorizationRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->gen_challenge == res.gen_challenge);
    }

    GIVEN("PaymentDetails on a contract session whose key cannot sign") {
        const auto seed_bad_key = [](D2StateHelper& helper) {
            seed_contract(helper);
            helper.get_context().pnc.contract.emplace().key_pem = "not a key";
        };
        auto pred = D2Predecessor::make<state::PaymentDetails>(ev::params::EvSessionParams{}, false, seed_bad_key);
        pred.stops_without_request(d2_ok<message_2::PaymentDetailsResponse>());
    }

    GIVEN("Authorization finished") {
        auto pred = D2Predecessor::of<state::Authorization>();
        const auto sent = pred.transitions_to(StateID::ChargeParameterDiscovery, authorization_finished());
        const auto req = sent.get<message_2::ChargeParameterDiscoveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->dc_ev_charge_parameter.has_value());
    }
}

SCENARIO("ISO15118-2 EV DC transitions send the successor's first request") {
    GIVEN("ChargeParameterDiscovery finished without CP-state feedback") {
        auto pred = D2Predecessor::of<state::ChargeParameterDiscovery>();
        const auto sent = pred.transitions_to(StateID::CableCheck, charge_parameter_discovery_finished());
        const auto req = sent.get<message_2::CableCheckRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->dc_ev_status.ev_ready);
    }

    GIVEN("ChargeParameterDiscovery finished with CP already in state C or D") {
        const auto seed_cp = [](D2StateHelper& helper) { helper.get_context().set_cp_state(true); };
        auto pred = D2Predecessor::make<state::ChargeParameterDiscovery>(ev::params::EvSessionParams{}, true, seed_cp);
        const auto sent = pred.transitions_to(StateID::CableCheck, charge_parameter_discovery_finished());
        REQUIRE(sent.get<message_2::CableCheckRequest>().has_value());
    }

    GIVEN("ChargeParameterDiscovery finished before CP reached state C or D") {
        auto pred =
            D2Predecessor::make<state::ChargeParameterDiscovery>(ev::params::EvSessionParams{}, true, d2_no_seed);
        pred.waits_in(StateID::CableCheck, charge_parameter_discovery_finished());

        WHEN("CP reaches state C") {
            pred.ctx.set_cp_state(true);
            const auto result = pred.control(ev::d20::CpState{true});

            THEN("the held CableCheckReq goes out") {
                REQUIRE(result.output == Disposition::Awaiting);
                const auto sent = take_all_requests(pred.helper->get_message_exchange());
                REQUIRE(sent.get<message_2::CableCheckRequest>().has_value());
            }
        }
    }

    GIVEN("CableCheck finished") {
        auto pred = D2Predecessor::of<state::ChargeParameterDiscovery>();
        pred.transitions_to(StateID::CableCheck, charge_parameter_discovery_finished());
        const auto sent = pred.transitions_to(StateID::PreCharge, cable_check_finished());
        const auto req = sent.get<message_2::PreChargeRequest>();
        REQUIRE(req.has_value());
        REQUIRE(dt::from_physical_value(req->ev_target_voltage) == 400.0);
    }

    GIVEN("PreCharge converged") {
        auto pred = D2Predecessor::of<state::PreCharge>();
        const auto sent = pred.transitions_to(StateID::PowerDelivery, pre_charge_converged());
        const auto req = sent.get<message_2::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::ChargeProgress::Start);
        REQUIRE(req->charging_profile.has_value());
    }

    GIVEN("PowerDelivery(Start) accepted") {
        auto pred = D2Predecessor::of<state::PowerDelivery>(state::PowerDelivery::Start{});
        const auto sent = pred.transitions_to(StateID::CurrentDemand, d2_ok<message_2::PowerDeliveryResponse>());
        const auto req = sent.get<message_2::CurrentDemandRequest>();
        REQUIRE(req.has_value());
        REQUIRE(dt::from_physical_value(req->ev_target_current) == 100.0);
    }

    GIVEN("PowerDelivery(Renegotiate) accepted") {
        auto pred = D2Predecessor::of<state::PowerDelivery>(state::PowerDelivery::Renegotiate{});
        const auto sent =
            pred.transitions_to(StateID::ChargeParameterDiscovery, d2_ok<message_2::PowerDeliveryResponse>());
        REQUIRE(sent.get<message_2::ChargeParameterDiscoveryRequest>().has_value());
    }

    GIVEN("CurrentDemand with the EVSE stopping") {
        auto pred = D2Predecessor::of<state::CurrentDemand>();
        const auto sent =
            pred.transitions_to(StateID::PowerDelivery, current_demand_with(dt::EVSENotification::StopCharging, false));
        const auto req = sent.get<message_2::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::ChargeProgress::Stop);
        REQUIRE_FALSE(req->dc_ev_power_delivery_parameter->dc_ev_status.ev_ready);
    }

    GIVEN("CurrentDemand asking for a receipt on a contract session") {
        auto pred = D2Predecessor::make<state::CurrentDemand>(ev::params::EvSessionParams{}, false, seed_contract);
        const auto sent =
            pred.transitions_to(StateID::MeteringReceipt, current_demand_with(dt::EVSENotification::None, true));
        const auto req = sent.get<message_2::MeteringReceiptRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->meter_info.meter_id == "METER-1");
    }

    GIVEN("CurrentDemand asking for a receipt with a key that cannot sign") {
        const auto seed_bad_key = [](D2StateHelper& helper) {
            seed_contract(helper);
            helper.get_context().pnc.contract.emplace().key_pem = "not a key";
        };
        auto pred = D2Predecessor::make<state::CurrentDemand>(ev::params::EvSessionParams{}, false, seed_bad_key);
        pred.stops_without_request(current_demand_with(dt::EVSENotification::None, true));
    }

    GIVEN("MeteringReceipt acknowledged") {
        dt::MeterInfo meter;
        meter.meter_id = "METER-1";
        auto pred = D2Predecessor::of<state::MeteringReceipt>(meter, std::nullopt);
        const auto sent = pred.transitions_to(StateID::CurrentDemand, d2_ok<message_2::MeteringReceiptResponse>());
        REQUIRE(sent.get<message_2::CurrentDemandRequest>().has_value());
    }

    GIVEN("PowerDelivery(Stop) accepted") {
        auto pred =
            D2Predecessor::of<state::PowerDelivery>(state::PowerDelivery::Stop{ev::session::StopReason::EvPause});
        const auto sent = pred.transitions_to(StateID::WeldingDetection, d2_ok<message_2::PowerDeliveryResponse>());
        const auto req = sent.get<message_2::WeldingDetectionRequest>();
        REQUIRE(req.has_value());
        REQUIRE_FALSE(req->dc_ev_status.ev_ready);
    }

    GIVEN("WeldingDetection at a safe voltage with a stop latched since the pause") {
        auto pred = D2Predecessor::of<state::WeldingDetection>(ev::session::StopReason::EvPause);
        pred.ctx.set_stop_charging_requested(true);
        const auto sent = pred.transitions_to(StateID::SessionStop, welding_safe());
        const auto req = sent.get<message_2::SessionStopRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charging_session == dt::ChargingSession::Terminate);
    }
}

SCENARIO("ISO15118-2 EV AC transitions send the successor's first request") {
    GIVEN("ChargeParameterDiscovery finished") {
        auto pred = D2Predecessor::make<state::ChargeParameterDiscovery>(ac_params(), false, d2_no_seed);
        const auto sent = pred.transitions_to(StateID::PowerDelivery, charge_parameter_discovery_finished());
        const auto req = sent.get<message_2::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::ChargeProgress::Start);
        REQUIRE(req->sa_schedule_tuple_id == 7);
        REQUIRE_FALSE(req->dc_ev_power_delivery_parameter.has_value());
    }

    GIVEN("PowerDelivery(Start) accepted") {
        auto pred =
            D2Predecessor::make<state::PowerDelivery>(ac_params(), false, d2_no_seed, state::PowerDelivery::Start{});
        const auto sent = pred.transitions_to(StateID::ChargingStatus, d2_ok<message_2::PowerDeliveryResponse>());
        REQUIRE(sent.get<message_2::ChargingStatusRequest>().has_value());
    }

    GIVEN("ChargingStatus with the EV pausing") {
        auto pred = D2Predecessor::make<state::ChargingStatus>(ac_params(), false, d2_no_seed);
        pred.ctx.set_pause_charging_requested(true);
        auto res = d2_ok<message_2::ChargingStatusResponse>();
        const auto sent = pred.transitions_to(StateID::PowerDelivery, res);
        const auto req = sent.get<message_2::PowerDeliveryRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charge_progress == dt::ChargeProgress::Stop);
    }

    GIVEN("PowerDelivery(Stop) accepted") {
        auto pred = D2Predecessor::make<state::PowerDelivery>(
            ac_params(), false, d2_no_seed, state::PowerDelivery::Stop{ev::session::StopReason::EvseTerminate});
        const auto sent = pred.transitions_to(StateID::SessionStop, d2_ok<message_2::PowerDeliveryResponse>());
        const auto req = sent.get<message_2::SessionStopRequest>();
        REQUIRE(req.has_value());
        REQUIRE(req->charging_session == dt::ChargingSession::Terminate);
    }

    GIVEN("MeteringReceipt acknowledged") {
        dt::MeterInfo meter;
        meter.meter_id = "METER-1";
        auto pred = D2Predecessor::make<state::MeteringReceipt>(ac_params(), false, d2_no_seed, meter, std::nullopt);
        const auto sent = pred.transitions_to(StateID::ChargingStatus, d2_ok<message_2::MeteringReceiptResponse>());
        REQUIRE(sent.get<message_2::ChargingStatusRequest>().has_value());
    }
}

TEST_CASE("ISO15118-2 EV SessionSetup reports a decode failure once") {
    const ev::feedback::Callbacks callbacks{};
    D2StateHelper helper{callbacks};
    auto& ctx = helper.get_context();
    fsm::v2::FSM<ev::d2::StateBase> fsm{ctx.create_state<ev::d2::state::SessionSetup>()};
    std::vector<std::string> errors;
    io::set_logging_callback([&](LogLevel level, const std::string& message) {
        if (level == LogLevel::Error) {
            errors.push_back(message);
        }
    });
    std::unique_ptr<message_2::ResponseVariant> response;
    std::string error;
    SECTION("undecodable response") {
        const std::array<uint8_t, 4> bytes{0xff, 0xff, 0xff, 0xff};
        response = std::make_unique<message_2::ResponseVariant>(io::StreamInputView{bytes.data(), bytes.size()});
        error = response->get_error();
    }
    SECTION("wrong response type still logs a mismatch") {
        response = std::make_unique<message_2::ResponseVariant>(message_2::PowerDeliveryResponse{});
        error = "Unexpected ISO 15118-2 response, got message type id:";
    }
    helper.get_message_exchange().set_response(std::move(response));
    const auto result = fsm.feed(ev::d2::Event::V2GTP_MESSAGE);
    io::set_logging_callback([](LogLevel, const std::string&) {});
    REQUIRE_FALSE(result.transitioned());
    REQUIRE(ctx.is_session_stopped());
    REQUIRE_FALSE(error.empty());
    REQUIRE(errors.size() == 1);
    REQUIRE(errors.front().find(error) != std::string::npos);
}
