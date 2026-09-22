// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/power_delivery.hpp>

#include <iso15118/ev/d2/state/charging_status.hpp>
#include <iso15118/ev/detail/d2/state/charging_status.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/charging_status.hpp>
#include <iso15118/message_2/metering_receipt.hpp>
#include <iso15118/message_2/power_delivery.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {

message_2::ChargingStatusResponse make_response(const message_2::Header& header, dt::ResponseCode code,
                                                dt::EVSENotification notification = dt::EVSENotification::None,
                                                bool receipt_required = false) {
    message_2::ChargingStatusResponse res;
    res.header = header;
    res.response_code = code;
    res.evse_id = "DE*PNX*E12345";
    res.sa_schedule_tuple_id = 7;
    res.evse_max_current = dt::to_physical_value(32.0, dt::Unit::A);
    res.ac_evse_status.notification = notification;
    if (receipt_required) {
        res.receipt_required = true;
        dt::MeterInfo info;
        info.meter_id = "METER1";
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
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, notification, true));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::MeteringReceipt);
    REQUIRE(primed.take_requests().template get<message_2::MeteringReceiptRequest>().has_value());

    primed.handle_response(d2_ok<message_2::MeteringReceiptResponse>());
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    return primed.take_requests();
}

} // namespace

SCENARIO("ISO15118-2 EV ChargingStatus's empty request goes out on the PowerDelivery transition") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargingStatus> primed{callbacks, ac_params(), false, d2_no_seed, by_power_delivery};

    const auto request = primed.take_requests().get<message_2::ChargingStatusRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == D2_SESSION_ID);
}

SCENARIO("ISO15118-2 EV ChargingStatus publishes the SECC max current and target power and loops") {
    std::optional<d20::AcTargetPower> target;
    std::optional<float> evse_max_current;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_target_power = [&target](const d20::AcTargetPower& t) { target = t; };
    callbacks.ac_evse_max_current = [&evse_max_current](float current) { evse_max_current = current; };
    const auto seed_voltage = [](D2StateHelper& helper) {
        helper.get_context().evse_info.ac_nominal_voltage = dt::to_physical_value(230.0, dt::Unit::V);
    };
    PrimedState<ev::d2::state::ChargingStatus> primed{callbacks, ac_params(), false, seed_voltage, by_power_delivery};

    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::d2::Disposition::Awaiting);
    REQUIRE(evse_max_current == 32.0f);
    REQUIRE(target.has_value());
    REQUIRE(target->target_active_power.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(target->target_active_power.value()) == 7360.0f);
    REQUIRE(target->target_active_power_L2.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(target->target_active_power_L2.value()) == 7360.0f);
    REQUIRE(target->target_active_power_L3.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(target->target_active_power_L3.value()) == 7360.0f);
    REQUIRE(primed.take_requests().get<message_2::ChargingStatusRequest>().has_value());
}

SCENARIO("ISO15118-2 EV ChargingStatus target power defaults to 230 V") {
    const auto target =
        ev::d2::state::charging_status::compute_ac_target_power(dt::to_physical_value(16.0, dt::Unit::A), std::nullopt);
    REQUIRE(target.target_active_power.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(target.target_active_power.value()) == 3680.0f);
    REQUIRE(target.target_active_power_L2.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(target.target_active_power_L2.value()) == 3680.0f);
    REQUIRE(target.target_active_power_L3.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(target.target_active_power_L3.value()) == 3680.0f);
}

SCENARIO("ISO15118-2 EV ChargingStatus diverts to PowerDelivery(Stop) on an EVSE StopCharging") {
    bool stop_from_charger = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.stop_from_charger = [&stop_from_charger]() { stop_from_charger = true; };
    PrimedState<ev::d2::state::ChargingStatus> primed{callbacks, ac_params(), false, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSENotification::StopCharging));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);

    REQUIRE(stop_from_charger);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PowerDelivery);
    const auto request = primed.take_requests().get<message_2::PowerDeliveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->charge_progress == dt::ChargeProgress::Stop);
    REQUIRE_FALSE(request->dc_ev_power_delivery_parameter.has_value());
    REQUIRE(walk_stop(primed).charging_session == dt::ChargingSession::Terminate);
}

SCENARIO("ISO15118-2 EV ChargingStatus pauses on a latched EV pause") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargingStatus> primed{callbacks, ac_params(), false, d2_no_seed};

    primed.ctx.set_pause_charging_requested(true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);

    const auto walk = walk_stop(primed);
    REQUIRE(walk.charging_session == dt::ChargingSession::Pause);
    REQUIRE(walk.paused);
}

SCENARIO("ISO15118-2 EV ChargingStatus terminates when an EVSE StopCharging follows an EV pause") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargingStatus> primed{callbacks, ac_params(), false, d2_no_seed};

    primed.ctx.set_pause_charging_requested(true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSENotification::StopCharging));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);

    const auto walk = walk_stop(primed);
    REQUIRE(walk.charging_session == dt::ChargingSession::Terminate);
    REQUIRE_FALSE(walk.paused);
}

SCENARIO("ISO15118-2 EV ChargingStatus keeps looping on ReceiptRequired without a contract") {
    std::vector<std::string> warnings;
    io::set_logging_callback([&warnings](LogLevel level, std::string message) {
        if (level == LogLevel::Warning) {
            warnings.push_back(std::move(message));
        }
    });
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargingStatus> primed{callbacks, ac_params(), false, d2_no_seed, by_power_delivery};

    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSENotification::None, true));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);
    io::set_logging_callback([](LogLevel, const std::string&) {});

    REQUIRE(result.output == ev::d2::Disposition::Awaiting);
    REQUIRE(primed.take_requests().get<message_2::ChargingStatusRequest>().has_value());
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings.front().find("ReceiptRequired") != std::string::npos);
}

SCENARIO("ISO15118-2 EV ChargingStatus detours to MeteringReceipt on a Contract session") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargingStatus> primed{callbacks, ac_params(), false, seed_contract};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSENotification::None, true));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::MeteringReceipt);
}

SCENARIO("ISO15118-2 EV ChargingStatus signs a due receipt before an EV stop [V2G2-512] [V2G2-513] [V2G2-519]") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargingStatus> primed{callbacks, ac_params(), false, seed_contract};

    primed.ctx.set_stop_charging_requested(true);
    const auto request = sign_then_respond(primed, dt::EVSENotification::None).get<message_2::PowerDeliveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->charge_progress == dt::ChargeProgress::Stop);
    const auto walk = walk_stop(primed);
    REQUIRE(walk.charging_session == dt::ChargingSession::Terminate);
    REQUIRE_FALSE(walk.paused);
}

SCENARIO("ISO15118-2 EV ChargingStatus carries an EV pause past a due receipt [V2G2-512] [V2G2-519]") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargingStatus> primed{callbacks, ac_params(), false, seed_contract};

    primed.ctx.set_pause_charging_requested(true);
    REQUIRE(sign_then_respond(primed, dt::EVSENotification::None).get<message_2::PowerDeliveryRequest>().has_value());
    const auto walk = walk_stop(primed);
    REQUIRE(walk.charging_session == dt::ChargingSession::Pause);
    REQUIRE(walk.paused);
}

SCENARIO("ISO15118-2 EV ChargingStatus signs a due receipt before an EVSE StopCharging [V2G2-512] [V2G2-519]") {
    int stop_from_charger = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.stop_from_charger = [&stop_from_charger]() { ++stop_from_charger; };
    PrimedState<ev::d2::state::ChargingStatus> primed{callbacks, ac_params(), false, seed_contract};

    const auto request =
        sign_then_respond(primed, dt::EVSENotification::StopCharging).get<message_2::PowerDeliveryRequest>();
    REQUIRE(stop_from_charger == 1);
    REQUIRE(request.has_value());
    REQUIRE(request->charge_progress == dt::ChargeProgress::Stop);
    REQUIRE(walk_stop(primed).charging_session == dt::ChargingSession::Terminate);
}

SCENARIO("ISO15118-2 EV ChargingStatus rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{ctx.create_state<ev::d2::state::ChargingStatus>()};
    };
    const auto make_ok = [](const message_2::Header& header) { return make_response(header, dt::ResponseCode::OK); };
    message_2::AuthorizationResponse wrong;
    wrong.header = d2_header();
    wrong.response_code = dt::ResponseCode::OK;
    wrong.evse_processing = dt::EVSEProcessing::Finished;
    check_rejection_paths(callbacks, ev::d2::StateID::ChargingStatus, make_fsm, make_ok, wrong, ac_params());
}
