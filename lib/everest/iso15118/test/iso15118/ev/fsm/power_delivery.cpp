// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::Progress;
using message_20::datatypes::ResponseCode;

message_20::PowerDeliveryResponse make_pd_res(const message_20::Header& header, ResponseCode code) {
    return message_20::PowerDeliveryResponse{header, code, std::nullopt};
}
} // namespace

SCENARIO("ISO15118-20 EV PowerDelivery sends Finished + chosen progress on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::PowerDelivery> primed{callbacks, no_seed, Progress::Start};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->processing == message_20::datatypes::Processing::Finished);
    REQUIRE(request_message->charge_progress == Progress::Start);
    REQUIRE_FALSE(request_message->power_profile.has_value());
    REQUIRE_FALSE(request_message->channel_selection.has_value());
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to DC_ChargeLoop on OK response") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::PowerDelivery> primed{callbacks, no_seed, Progress::Start};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to DC_WeldingDetection on Stop") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::PowerDelivery> primed{callbacks, no_seed, Progress::Stop};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_WeldingDetection);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to DC_ChargeLoop on OK response for DC_BPT") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_dc_bpt = [](FsmStateHelper& helper) {
        helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::DC_BPT);
    };
    PrimedState<ev::d20::state::PowerDelivery> primed{callbacks, seed_dc_bpt, Progress::Start};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to DC_WeldingDetection on Stop for DC_BPT") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_dc_bpt = [](FsmStateHelper& helper) {
        helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::DC_BPT);
    };
    PrimedState<ev::d20::state::PowerDelivery> primed{callbacks, seed_dc_bpt, Progress::Stop};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_WeldingDetection);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to AC_ChargeLoop on OK response for AC") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_ac = [](FsmStateHelper& helper) {
        helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::AC);
    };
    PrimedState<ev::d20::state::PowerDelivery> primed{callbacks, seed_ac, Progress::Start};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to SessionStop on Stop for AC") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_ac = [](FsmStateHelper& helper) {
        helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::AC);
    };
    PrimedState<ev::d20::state::PowerDelivery> primed{callbacks, seed_ac, Progress::Stop};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to AC_ChargeLoop on OK response for AC_BPT") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_ac_bpt = [](FsmStateHelper& helper) {
        helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::AC_BPT);
    };
    PrimedState<ev::d20::state::PowerDelivery> primed{callbacks, seed_ac_bpt, Progress::Start};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::AC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery transitions to SessionStop on Stop for AC_BPT") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_ac_bpt = [](FsmStateHelper& helper) {
        helper.get_context().set_selected_service(message_20::datatypes::ServiceCategory::AC_BPT);
    };
    PrimedState<ev::d20::state::PowerDelivery> primed{callbacks, seed_ac_bpt, Progress::Stop};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery accepts OK_PowerToleranceConfirmed") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::PowerDelivery> primed{callbacks, no_seed, Progress::Start};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::OK_PowerToleranceConfirmed));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_ChargeLoop);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery accepts WARNING_StandbyNotAllowed") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::PowerDelivery> primed{callbacks, no_seed, Progress::Standby};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::WARNING_StandbyNotAllowed));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV PowerDelivery stops session on FAILED_ContactorError") {
    // State-specific rejection beyond the shared triple.
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::PowerDelivery> primed{callbacks, no_seed, Progress::Start};

    primed.handle_response(make_pd_res(SESSION_HEADER, ResponseCode::FAILED_ContactorError));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV PowerDelivery rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::PowerDelivery>(Progress::Start)};
    };
    const auto make_ok = [](const message_20::Header& header) { return make_pd_res(header, ResponseCode::OK); };
    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    check_rejection_paths(callbacks, ev::d20::StateID::PowerDelivery, make_fsm, make_ok, wrong);
}
