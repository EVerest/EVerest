// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/service_discovery.hpp>
#include <iso15118/message/service_detail.hpp>
#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
constexpr auto SESSION_HEADER =
    message_20::Header{std::array<uint8_t, 8>{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};

message_20::ServiceDiscoveryResponse make_response(message_20::datatypes::ResponseCode code,
                                                   message_20::datatypes::ServiceCategory offered) {
    message_20::ServiceDiscoveryResponse res{};
    res.header.session_id = SESSION_HEADER.session_id;
    res.response_code = code;
    res.energy_transfer_service_list = {{offered, false}};
    return res;
}
} // namespace

SCENARIO("ISO15118-20 EV ServiceDiscovery transitions to ServiceDetail when DC offered") {
    const ev::d20::session::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::ServiceDiscovery>()};

    state_helper.handle_response(
        make_response(message_20::datatypes::ResponseCode::OK, message_20::datatypes::ServiceCategory::DC));

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceDetail);
    REQUIRE(ctx.is_session_stopped() == false);

    const auto request_message = ctx.get_request<message_20::ServiceDetailRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->service == message_20::to_underlying_value(message_20::datatypes::ServiceCategory::DC));
}

SCENARIO("ISO15118-20 EV ServiceDiscovery stops session when DC not offered") {
    const ev::d20::session::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::ServiceDiscovery>()};

    state_helper.handle_response(
        make_response(message_20::datatypes::ResponseCode::OK, message_20::datatypes::ServiceCategory::AC));

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceDiscovery);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV ServiceDiscovery stops session on FAILED response") {
    const ev::d20::session::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::ServiceDiscovery>()};

    state_helper.handle_response(
        make_response(message_20::datatypes::ResponseCode::FAILED, message_20::datatypes::ServiceCategory::DC));

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceDiscovery);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV ServiceDiscovery stops session on wrong variant") {
    const ev::d20::session::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::ServiceDiscovery>()};

    state_helper.handle_response(message_20::ServiceDetailResponse{});

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceDiscovery);
    REQUIRE(ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV ServiceDiscovery stops session on mismatched response session_id") {
    const ev::d20::session::feedback::Callbacks callbacks{};
    auto state_helper = FsmStateHelper(callbacks);
    auto& ctx = state_helper.get_context();
    ctx.get_session().set_id(SESSION_HEADER.session_id);

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::ServiceDiscovery>()};

    auto res = make_response(message_20::datatypes::ResponseCode::OK, message_20::datatypes::ServiceCategory::DC);
    res.header.session_id = message_20::datatypes::SessionId{0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00};
    state_helper.handle_response(res);

    const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::ServiceDiscovery);
    REQUIRE(ctx.is_session_stopped() == true);
}
