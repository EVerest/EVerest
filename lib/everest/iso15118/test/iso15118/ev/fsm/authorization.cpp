// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/authorization.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/service_discovery.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::Processing;
using message_20::datatypes::ResponseCode;

// Authorization reads the EIM auth service the EVSE advertised in AuthorizationSetup.
const auto seed_eim = [](FsmStateHelper& helper) {
    helper.get_context().get_evse_session_info().auth_services = {message_20::datatypes::Authorization::EIM};
};

message_20::AuthorizationResponse make_auth_res(const message_20::Header& header, ResponseCode code,
                                                Processing processing) {
    return message_20::AuthorizationResponse{header, code, processing};
}
} // namespace

SCENARIO("ISO15118-20 EV Authorization transitions to ServiceDiscovery on Finished") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::Authorization> primed{callbacks, seed_eim};

    primed.handle_response(make_auth_res(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ServiceDiscovery);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ServiceDiscoveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV Authorization stays and resends on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::Authorization> primed{callbacks, seed_eim};

    primed.handle_response(make_auth_res(SESSION_HEADER, ResponseCode::OK, Processing::Ongoing));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::Authorization);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::AuthorizationRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->selected_authorization_service == message_20::datatypes::Authorization::EIM);
}

SCENARIO("ISO15118-20 EV Authorization rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        seed_eim(helper);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::Authorization>()};
    };
    const auto make_ok = [](const message_20::Header& header) {
        return make_auth_res(header, ResponseCode::OK, Processing::Finished);
    };
    check_rejection_paths(callbacks, ev::d20::StateID::Authorization, make_fsm, make_ok,
                          message_20::ServiceDiscoveryResponse{});
}
