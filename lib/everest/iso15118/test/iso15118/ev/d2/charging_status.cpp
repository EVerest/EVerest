// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/charging_status.hpp>
#include <iso15118/ev/detail/d2/state/charging_status.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/charging_status.hpp>
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

} // namespace

SCENARIO("ISO15118-2 EV ChargingStatus sends an empty request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargingStatus> primed{callbacks, ac_params(), false, d2_no_seed};

    const auto request = primed.take_requests().get<message_2::ChargingStatusRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == D2_SESSION_ID);
}

SCENARIO("ISO15118-2 EV ChargingStatus publishes the SECC target power and loops") {
    std::optional<d20::AcTargetPower> target;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_target_power = [&target](const d20::AcTargetPower& t) { target = t; };
    const auto seed_voltage = [](D2StateHelper& helper) {
        helper.get_context().evse_info.ac_nominal_voltage = dt::to_physical_value(230.0, dt::Unit::V);
    };
    PrimedState<ev::d2::state::ChargingStatus> primed{callbacks, ac_params(), false, seed_voltage};

    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::d2::Disposition::Awaiting);
    REQUIRE(target.has_value());
    REQUIRE(target->target_active_power.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(target->target_active_power.value()) == 7360.0f);
    REQUIRE(primed.take_requests().get<message_2::ChargingStatusRequest>().has_value());
}

SCENARIO("ISO15118-2 EV ChargingStatus target power defaults to 230 V") {
    const auto target =
        ev::d2::state::charging_status::compute_ac_target_power(dt::to_physical_value(16.0, dt::Unit::A), std::nullopt);
    REQUIRE(target.target_active_power.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(target.target_active_power.value()) == 3680.0f);
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
}

SCENARIO("ISO15118-2 EV ChargingStatus keeps looping on ReceiptRequired without a contract") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::ChargingStatus> primed{callbacks, ac_params(), false, d2_no_seed};

    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSENotification::None, true));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).output == ev::d2::Disposition::Awaiting);
}

SCENARIO("ISO15118-2 EV ChargingStatus detours to MeteringReceipt on a Contract session") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_contract = [](D2StateHelper& helper) {
        auto& pnc = helper.get_context().pnc;
        pnc.contract_selected = true;
        pnc.contract_key_pem = make_test_ec_key_pem();
    };
    PrimedState<ev::d2::state::ChargingStatus> primed{callbacks, ac_params(), false, seed_contract};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSENotification::None, true));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::MeteringReceipt);
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
