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
using message_20::datatypes::ResponseCode;
using message_20::datatypes::ServiceCategory;

message_20::ServiceDiscoveryResponse make_response(const message_20::Header& header, ResponseCode code,
                                                   ServiceCategory offered) {
    message_20::ServiceDiscoveryResponse res{};
    res.header = header;
    res.response_code = code;
    res.energy_transfer_service_list = {{offered, false}};
    return res;
}
} // namespace

SCENARIO("ISO15118-20 EV ServiceDiscovery transitions to ServiceDetail when DC offered") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDiscovery> primed{callbacks, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::DC));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceDetail);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ServiceDetailRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->service == message_20::to_underlying_value(ServiceCategory::DC));
}

SCENARIO("ISO15118-20 EV ServiceDiscovery stops session when DC not offered") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDiscovery> primed{callbacks, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == true);
}

namespace {
const auto seed_ac = [](FsmStateHelper& helper) { helper.get_context().set_selected_service(ServiceCategory::AC); };
} // namespace

SCENARIO("ISO15118-20 EV ServiceDiscovery lists the configured AC service") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDiscovery> primed{callbacks, seed_ac};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ServiceDiscoveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->supported_service_ids.has_value());
    REQUIRE(request_message->supported_service_ids->size() == 1);
    REQUIRE(request_message->supported_service_ids->front() == message_20::to_underlying_value(ServiceCategory::AC));
}

SCENARIO("ISO15118-20 EV ServiceDiscovery transitions to ServiceDetail when AC offered") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDiscovery> primed{callbacks, seed_ac};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::AC));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceDetail);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ServiceDetailRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->service == message_20::to_underlying_value(ServiceCategory::AC));
}

SCENARIO("ISO15118-20 EV ServiceDiscovery stops session when AC not offered") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ServiceDiscovery> primed{callbacks, seed_ac};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, ServiceCategory::DC));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV ServiceDiscovery rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ServiceDiscovery>()};
    };
    const auto make_ok = [](const message_20::Header& header) {
        return make_response(header, ResponseCode::OK, ServiceCategory::DC);
    };
    check_rejection_paths(callbacks, ev::d20::StateID::ServiceDiscovery, make_fsm, make_ok,
                          message_20::ServiceDetailResponse{});
}
