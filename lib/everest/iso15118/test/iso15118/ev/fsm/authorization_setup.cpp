// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/authorization_setup.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 EV authorization setup state transitions") {

    const ev::d20::session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(callbacks);

    auto ctx = state_helper.get_context();

    GIVEN("Good case - authorization setup response with OK and EIM") {

        // setup the state and context to something reasonable
        const auto header = message_20::Header{{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};

        ctx.get_session().set_id(header.session_id);

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::AuthorizationSetup>()};

        const auto res = message_20::AuthorizationSetupResponse{
            header, message_20::datatypes::ResponseCode::OK, {message_20::datatypes::Authorization::EIM}, false};

        state_helper.handle_response(res);

        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("Check if passes to authorization state and sends EIM AuthorizationRequest") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::Authorization);

            const auto request_message = ctx.get_request<message_20::AuthorizationRequest>();
            REQUIRE(request_message.has_value());

            const auto& request = request_message.value();
            REQUIRE(request.header.session_id == header.session_id);
            REQUIRE(request.selected_authorization_service == message_20::datatypes::Authorization::EIM);
            REQUIRE(
                std::holds_alternative<message_20::datatypes::EIM_ASReqAuthorizationMode>(request.authorization_mode));
        }
    }

    // PnC is not yet implemented properly in the EV, but we can at least test that the state machine transitions
    // correctly
    GIVEN("Good case - authorization setup response with OK and PnC") {

        // setup the state and context to something reasonable
        const auto header = message_20::Header{{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};

        ctx.get_session().set_id(header.session_id);

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::AuthorizationSetup>()};

        const auto res = message_20::AuthorizationSetupResponse{
            header, message_20::datatypes::ResponseCode::OK, {message_20::datatypes::Authorization::PnC}, false};

        state_helper.handle_response(res);

        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("Check if passes to authorization state and sends Pnc AuthorizationRequest") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::Authorization);

            const auto request_message = ctx.get_request<message_20::AuthorizationRequest>();
            REQUIRE(request_message.has_value());

            const auto& request = request_message.value();
            REQUIRE(request.header.session_id == header.session_id);
            REQUIRE(request.selected_authorization_service == message_20::datatypes::Authorization::PnC);
            REQUIRE(
                std::holds_alternative<message_20::datatypes::PnC_ASReqAuthorizationMode>(request.authorization_mode));
            // TODO(RB): Add checks for PnC authorization mode
        }
    }
    // TODO(RB): Add more test cases (bad response codes, unsupported authorization modes,
    // more than one authorization mode, certificate installation service, etc)

    GIVEN("Bad case - authorization setup response with FAILED and EIM") {

        // setup the state and context to something reasonable
        const auto header = message_20::Header{{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};

        ctx.get_session().set_id(header.session_id);

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::AuthorizationSetup>()};

        const auto res = message_20::AuthorizationSetupResponse{
            header, message_20::datatypes::ResponseCode::FAILED, {message_20::datatypes::Authorization::EIM}, false};

        state_helper.handle_response(res);

        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("Check if passes to authorization state and sends EIM AuthorizationRequest") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::AuthorizationSetup);
            REQUIRE(ctx.is_session_stopped() == true);
        }
    }
}
