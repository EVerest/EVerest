// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/dc_pre_charge.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::ResponseCode;

// DC_PreCharge reads target_voltage from the DC params to size its request and its
// in-tolerance check.
const auto seed_target_400 = [](FsmStateHelper& helper) {
    ev::DcChargeParams params{};
    params.target_voltage = 400.0f;
    helper.set_dc_params(params);
};

message_20::DC_PreChargeResponse make_response(const message_20::Header& header, ResponseCode code,
                                               message_20::datatypes::RationalNumber present_voltage) {
    return message_20::DC_PreChargeResponse{header, code, present_voltage};
}
} // namespace

SCENARIO("ISO15118-20 EV DC_PreCharge sends initial Starting request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_PreCharge> primed{callbacks, seed_target_400};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_PreChargeRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->processing == message_20::datatypes::Processing::Ongoing);
    REQUIRE(message_20::datatypes::from_RationalNumber(request_message->target_voltage) == Catch::Approx(400.0f));
}

SCENARIO("ISO15118-20 EV DC_PreCharge fires dc_power_on and transitions to PowerDelivery on in-tolerance response") {
    bool dc_power_on_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.dc_power_on = [&dc_power_on_fired]() { dc_power_on_fired = true; };
    PrimedState<ev::d20::state::DC_PreCharge> primed{callbacks, seed_target_400};

    // enter() queues an Ongoing DC_PreChargeRequest; a Finished one is never emitted.
    {
        const auto enter_requests = primed.take_requests();
        const auto pre_charge_request = enter_requests.get<message_20::DC_PreChargeRequest>();
        REQUIRE(pre_charge_request.has_value());
        REQUIRE(pre_charge_request->processing == message_20::datatypes::Processing::Ongoing);
    }

    // SECC reports present voltage in tolerance of the 400 V target.
    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, message_20::datatypes::from_float(400.0f)));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(dc_power_on_fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    // The transition itself signals "finished". PowerDelivery::enter() queued a
    // PowerDeliveryRequest(Start); no new precharge request was emitted by feed().
    const auto requests = primed.take_requests();
    REQUIRE_FALSE(requests.get<message_20::DC_PreChargeRequest>().has_value());

    const auto pd_request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(pd_request.has_value());
    REQUIRE(pd_request->charge_progress == message_20::datatypes::Progress::Start);
}

SCENARIO("ISO15118-20 EV DC_PreCharge resends Ongoing request when voltage not in tolerance") {
    bool dc_power_on_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.dc_power_on = [&dc_power_on_fired]() { dc_power_on_fired = true; };
    PrimedState<ev::d20::state::DC_PreCharge> primed{callbacks, seed_target_400};

    // SECC reports present voltage far below the 400 V target; precharge not complete.
    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, message_20::datatypes::from_float(100.0f)));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(dc_power_on_fired == false);
    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_PreCharge);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    // A single Ongoing DC_PreChargeRequest is re-emitted; no PowerDelivery transition.
    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_PreChargeRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->processing == message_20::datatypes::Processing::Ongoing);
    REQUIRE_FALSE(requests.get<message_20::PowerDeliveryRequest>().has_value());
}

SCENARIO("ISO15118-20 EV DC_PreCharge stops session on FAILED_UnknownSession") {
    // State-specific rejection beyond the shared triple.
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_PreCharge> primed{callbacks, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::FAILED_UnknownSession,
                                         message_20::datatypes::RationalNumber{0, 0}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_PreCharge);
    REQUIRE(primed.ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_PreCharge rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::DC_PreCharge>()};
    };
    const auto make_ok = [](const message_20::Header& header) {
        return make_response(header, ResponseCode::OK, message_20::datatypes::RationalNumber{0, 0});
    };
    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    check_rejection_paths(callbacks, ev::d20::StateID::DC_PreCharge, make_fsm, make_ok, wrong);
}
