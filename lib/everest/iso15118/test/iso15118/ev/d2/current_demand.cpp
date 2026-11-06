// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/power_delivery.hpp>

#include <iso15118/ev/d2/state/current_demand.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/current_demand.hpp>
#include <iso15118/message_2/metering_receipt.hpp>
#include <iso15118/message_2/power_delivery.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {

message_2::CurrentDemandResponse make_response(const message_2::Header& header, dt::ResponseCode code,
                                               dt::EVSENotification notification = dt::EVSENotification::None,
                                               dt::DC_EVSEStatusCode status = dt::DC_EVSEStatusCode::EVSE_Ready,
                                               bool receipt_required = false) {
    message_2::CurrentDemandResponse res;
    res.header = header;
    res.response_code = code;
    res.dc_evse_status.notification = notification;
    res.dc_evse_status.status_code = status;
    res.evse_present_voltage = dt::to_physical_value(400.0, dt::Unit::V);
    res.evse_present_current = dt::to_physical_value(100.0, dt::Unit::A);
    res.evse_maximum_voltage_limit = dt::to_physical_value(900.0, dt::Unit::V);
    res.evse_maximum_current_limit = dt::to_physical_value(200.0, dt::Unit::A);
    res.evse_maximum_power_limit = dt::to_physical_value(150000.0, dt::Unit::W);
    res.sa_schedule_tuple_id = 7;
    if (receipt_required) {
        res.receipt_required = true;
        dt::MeterInfo info;
        info.meter_id = "METER1";
        info.meter_reading = 1234;
        res.meter_info = info;
    }
    return res;
}

const auto by_power_delivery = d2_entered_by([](D2StateHelper& helper) {
    return d2_successor_of<ev::d2::state::PowerDelivery>(helper, d2_ok<message_2::PowerDeliveryResponse>(),
                                                         ev::d2::state::PowerDelivery::Start{});
});

const auto seed_contract = [](D2StateHelper& helper) {
    auto& pnc = helper.get_context().pnc;
    pnc.contract_selected = true;
    pnc.contract.emplace().key_pem = make_test_ec_key_pem();
};

// Feeds a ReceiptRequired response carrying `notification`, then accepts the MeteringReceiptReq it
// must send; returns the request that follows the MeteringReceiptRes.
template <typename Primed> DecodedRequests sign_then_respond(Primed& primed, dt::EVSENotification notification) {
    primed.handle_response(
        make_response(d2_header(), dt::ResponseCode::OK, notification, dt::DC_EVSEStatusCode::EVSE_Ready, true));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::MeteringReceipt);
    REQUIRE(primed.take_requests().template get<message_2::MeteringReceiptRequest>().has_value());

    primed.handle_response(d2_ok<message_2::MeteringReceiptResponse>());
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    return primed.take_requests();
}

} // namespace

SCENARIO("ISO15118-2 EV CurrentDemand's request carries the module set points from the PowerDelivery transition") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, d2_no_seed, by_power_delivery};

    const auto request = primed.take_requests().get<message_2::CurrentDemandRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == D2_SESSION_ID);
    REQUIRE(dt::from_physical_value(request->ev_target_voltage) == 400.0);
    REQUIRE(dt::from_physical_value(request->ev_target_current) == 100.0);
    REQUIRE(request->ev_maximum_current_limit.has_value());
    REQUIRE(request->ev_maximum_power_limit.has_value());
    REQUIRE(request->charging_complete == false);
}

SCENARIO("ISO15118-2 EV CurrentDemand loops and republishes the SECC limits") {
    std::optional<ev::feedback::DcMaximumLimits> limits;
    ev::feedback::Callbacks callbacks{};
    callbacks.dc_evse_present_limits = [&limits](const ev::feedback::DcMaximumLimits& l) { limits = l; };
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, d2_no_seed, by_power_delivery};

    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::d2::Disposition::Awaiting);
    REQUIRE(limits.has_value());
    REQUIRE(limits->current == 200.0f);
    REQUIRE(primed.take_requests().get<message_2::CurrentDemandRequest>().has_value());
}

SCENARIO("ISO15118-2 EV CurrentDemand diverts to PowerDelivery(Stop) on an EVSE StopCharging") {
    bool stop_from_charger = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.stop_from_charger = [&stop_from_charger]() { stop_from_charger = true; };
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSENotification::StopCharging));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);

    REQUIRE(stop_from_charger);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PowerDelivery);
    const auto request = primed.take_requests().get<message_2::PowerDeliveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->charge_progress == dt::ChargeProgress::Stop);
    REQUIRE(walk_stop(primed).charging_session == dt::ChargingSession::Terminate);
}

SCENARIO("ISO15118-2 EV CurrentDemand diverts on a non-ready DC_EVSEStatus") {
    bool stop_from_charger = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.stop_from_charger = [&stop_from_charger]() { stop_from_charger = true; };
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSENotification::None,
                                         dt::DC_EVSEStatusCode::EVSE_EmergencyShutdown));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(stop_from_charger);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PowerDelivery);
}

SCENARIO("ISO15118-2 EV CurrentDemand stops on an unsupported ReNegotiation") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSENotification::ReNegotiation));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(walk_stop(primed).charging_session == dt::ChargingSession::Terminate);
}

SCENARIO("ISO15118-2 EV CurrentDemand diverts to PowerDelivery(Stop) on a latched EV pause") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, d2_no_seed};

    primed.ctx.set_pause_charging_requested(true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);

    const auto walk = walk_stop(primed);
    REQUIRE(walk.charging_session == dt::ChargingSession::Pause);
    REQUIRE(walk.paused);
}

SCENARIO("ISO15118-2 EV CurrentDemand terminates on an EV stop latched during the stop walk after an EV pause") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, d2_no_seed};

    primed.ctx.set_pause_charging_requested(true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);

    // The driver ends the session while PowerDelivery(Stop) is outstanding.
    primed.ctx.set_stop_charging_requested(true);

    const auto walk = walk_stop(primed);
    REQUIRE(walk.charging_session == dt::ChargingSession::Terminate);
    REQUIRE_FALSE(walk.paused);
}

SCENARIO("ISO15118-2 EV CurrentDemand terminates when an EVSE StopCharging follows an EV pause") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, d2_no_seed};

    primed.ctx.set_pause_charging_requested(true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSENotification::StopCharging));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);

    const auto walk = walk_stop(primed);
    REQUIRE(walk.charging_session == dt::ChargingSession::Terminate);
    REQUIRE_FALSE(walk.paused);
}

SCENARIO("ISO15118-2 EV CurrentDemand keeps looping on ReceiptRequired without a contract") {
    std::vector<std::string> warnings;
    io::set_logging_callback([&warnings](LogLevel level, std::string message) {
        if (level == LogLevel::Warning) {
            warnings.push_back(std::move(message));
        }
    });
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, d2_no_seed, by_power_delivery};

    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSENotification::None,
                                         dt::DC_EVSEStatusCode::EVSE_Ready, true));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);
    io::set_logging_callback([](LogLevel, const std::string&) {});

    REQUIRE(result.output == ev::d2::Disposition::Awaiting);
    REQUIRE(primed.take_requests().get<message_2::CurrentDemandRequest>().has_value());
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings.front().find("ReceiptRequired") != std::string::npos);
}

SCENARIO("ISO15118-2 EV CurrentDemand detours to MeteringReceipt on a Contract session") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, seed_contract};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSENotification::None,
                                         dt::DC_EVSEStatusCode::EVSE_Ready, true));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::MeteringReceipt);
    REQUIRE(primed.take_requests().get<message_2::MeteringReceiptRequest>().has_value());
}

SCENARIO("ISO15118-2 EV CurrentDemand signs a due receipt before an EV stop [V2G2-790] [V2G2-793]") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, seed_contract};

    primed.ctx.set_stop_charging_requested(true);
    const auto request = sign_then_respond(primed, dt::EVSENotification::None).get<message_2::PowerDeliveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->charge_progress == dt::ChargeProgress::Stop);
    const auto walk = walk_stop(primed);
    REQUIRE(walk.charging_session == dt::ChargingSession::Terminate);
    REQUIRE_FALSE(walk.paused);
}

SCENARIO("ISO15118-2 EV CurrentDemand carries an EV pause past a due receipt [V2G2-790] [V2G2-793]") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, seed_contract};

    primed.ctx.set_pause_charging_requested(true);
    REQUIRE(sign_then_respond(primed, dt::EVSENotification::None).get<message_2::PowerDeliveryRequest>().has_value());
    const auto walk = walk_stop(primed);
    REQUIRE(walk.charging_session == dt::ChargingSession::Pause);
    REQUIRE(walk.paused);
}

SCENARIO("ISO15118-2 EV CurrentDemand signs a due receipt before an EVSE StopCharging [V2G2-790] [V2G2-793]") {
    int stop_from_charger = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.stop_from_charger = [&stop_from_charger]() { ++stop_from_charger; };
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, seed_contract};

    const auto request =
        sign_then_respond(primed, dt::EVSENotification::StopCharging).get<message_2::PowerDeliveryRequest>();
    REQUIRE(stop_from_charger == 1);
    REQUIRE(request.has_value());
    REQUIRE(request->charge_progress == dt::ChargeProgress::Stop);
    REQUIRE(walk_stop(primed).charging_session == dt::ChargingSession::Terminate);
}

SCENARIO("ISO15118-2 EV CurrentDemand rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{ctx.create_state<ev::d2::state::CurrentDemand>()};
    };
    const auto make_ok = [](const message_2::Header& header) { return make_response(header, dt::ResponseCode::OK); };
    message_2::AuthorizationResponse wrong;
    wrong.header = d2_header();
    wrong.response_code = dt::ResponseCode::OK;
    wrong.evse_processing = dt::EVSEProcessing::Finished;
    check_rejection_paths(callbacks, ev::d2::StateID::CurrentDemand, make_fsm, make_ok, wrong);
}
