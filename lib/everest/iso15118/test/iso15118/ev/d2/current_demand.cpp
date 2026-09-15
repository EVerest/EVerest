// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/current_demand.hpp>
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

} // namespace

SCENARIO("ISO15118-2 EV CurrentDemand sends the module set points on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, d2_no_seed};

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
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, d2_no_seed};

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
    REQUIRE(primed.ctx.is_stop_charging_requested() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PowerDelivery);
    const auto request = primed.take_requests().get<message_2::PowerDeliveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->charge_progress == dt::ChargeProgress::Stop);
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
    REQUIRE(primed.ctx.is_stop_charging_requested() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PowerDelivery);
}

SCENARIO("ISO15118-2 EV CurrentDemand diverts to PowerDelivery(Stop) on a latched EV pause") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, d2_no_seed};

    primed.ctx.set_pause_charging_requested(true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PowerDelivery);
}

SCENARIO("ISO15118-2 EV CurrentDemand keeps looping on ReceiptRequired without a contract") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, d2_no_seed};

    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSENotification::None,
                                         dt::DC_EVSEStatusCode::EVSE_Ready, true));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::d2::Disposition::Awaiting);
    REQUIRE(primed.take_requests().get<message_2::CurrentDemandRequest>().has_value());
}

SCENARIO("ISO15118-2 EV CurrentDemand detours to MeteringReceipt on a Contract session") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_contract = [](D2StateHelper& helper) {
        auto& pnc = helper.get_context().pnc;
        pnc.contract_selected = true;
        pnc.contract_key_pem = make_test_ec_key_pem();
    };
    PrimedState<ev::d2::state::CurrentDemand> primed{callbacks, seed_contract};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSENotification::None,
                                         dt::DC_EVSEStatusCode::EVSE_Ready, true));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::MeteringReceipt);
    REQUIRE(primed.take_requests().get<message_2::MeteringReceiptRequest>().has_value());
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
