// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/authorization.hpp>
#include <iso15118/d20/state/service_discovery.hpp>

#include <iso15118/message/authorization.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 authorization state transitions") {

    const d20::EvseSetupConfig evse_setup = create_default_evse_setup();

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};

    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();
    ctx.session = d20::Session();

    GIVEN("Bad Case - Unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::Authorization>()};

        ctx.session_config.cert_install_service = false;
        ctx.session_config.authorization_services = {dt::Authorization::EIM};

        message_20::AuthorizationRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;
        req.selected_authorization_service = dt::Authorization::EIM;
        req.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::AuthorizationResponse>();
            REQUIRE(response_message.has_value());

            const auto& authorization_setup_res = response_message.value();

            REQUIRE(authorization_setup_res.response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(authorization_setup_res.evse_processing == dt::Processing::Finished);
        }
    }

    GIVEN("Warning - Authorization selection is invalid") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::Authorization>()};

        ctx.session.offered_services.auth_services = {dt::Authorization::PnC};

        message_20::AuthorizationRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_authorization_service = dt::Authorization::EIM;
        req.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);

            const auto response_message = ctx.get_response<message_20::AuthorizationResponse>();
            REQUIRE(response_message.has_value());

            const auto& authorization_setup_res = response_message.value();

            REQUIRE(authorization_setup_res.response_code == dt::ResponseCode::WARNING_AuthorizationSelectionInvalid);
            REQUIRE(authorization_setup_res.evse_processing == dt::Processing::Finished);
        }
    }

    GIVEN("Warning - EIM Authorization Failure") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::Authorization>()};

        ctx.session.offered_services.auth_services = {dt::Authorization::EIM};

        state_helper.set_active_control_event(d20::AuthorizationResponse{false});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::AuthorizationRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_authorization_service = dt::Authorization::EIM;
        req.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);

            const auto response_message = ctx.get_response<message_20::AuthorizationResponse>();
            REQUIRE(response_message.has_value());

            const auto& authorization_setup_res = response_message.value();

            REQUIRE(authorization_setup_res.response_code == dt::ResponseCode::WARNING_EIMAuthorizationFailure);
            REQUIRE(authorization_setup_res.evse_processing == dt::Processing::Finished);
        }
    }

    GIVEN("Good case - EIM waiting for authorization") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::Authorization>()};

        ctx.session.offered_services.auth_services = {dt::Authorization::EIM};

        message_20::AuthorizationRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        req.selected_authorization_service = dt::Authorization::EIM;
        req.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);

            const auto response_message = ctx.get_response<message_20::AuthorizationResponse>();
            REQUIRE(response_message.has_value());

            const auto& authorization_setup_res = response_message.value();

            REQUIRE(authorization_setup_res.response_code == dt::ResponseCode::OK);
            REQUIRE(authorization_setup_res.evse_processing == dt::Processing::Ongoing);
        }
    }

    GIVEN("Good case - EIM authorized") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::Authorization>()};

        ctx.session.offered_services.auth_services = {dt::Authorization::EIM};

        state_helper.set_active_control_event(d20::AuthorizationResponse{true});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::AuthorizationRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.selected_authorization_service = dt::Authorization::EIM;
        req.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ServiceDiscovery);

            const auto response_message = ctx.get_response<message_20::AuthorizationResponse>();
            REQUIRE(response_message.has_value());

            const auto& authorization_res = response_message.value();

            REQUIRE(authorization_res.response_code == dt::ResponseCode::OK);
            REQUIRE(authorization_res.evse_processing == dt::Processing::Finished);
        }
    }

    GIVEN("TIMEOUT Event") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::Authorization>()};

        ctx.session.offered_services.auth_services = {dt::Authorization::EIM};

        message_20::AuthorizationRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        req.selected_authorization_service = dt::Authorization::EIM;
        req.authorization_mode.emplace<dt::EIM_ASReqAuthorizationMode>();

        // Send first req message
        state_helper.handle_request(req);
        auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        // Simulate a timeout
        ctx.set_active_timeout(d20::TimeoutType::ONGOING);
        result = fsm.feed(d20::Event::TIMEOUT);

        // Send another req message
        state_helper.handle_request(req);
        result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);

            const auto response_message = ctx.get_response<message_20::AuthorizationResponse>();
            REQUIRE(response_message.has_value());

            const auto& authorization_setup_res = response_message.value();

            REQUIRE(authorization_setup_res.response_code == dt::ResponseCode::FAILED);
            REQUIRE(authorization_setup_res.evse_processing == dt::Processing::Finished);
        }
    }

    GIVEN("Event then other V2GTP_MESSAGE") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::Authorization>()};
        const auto result = fsm.feed(d20::Event::FAILED);
        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);
        }
    }

    GIVEN("SessionStopReq") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::Authorization>()};

        // Setting up session_config based on test
        ctx.session_config.cert_install_service = false;
        ctx.session_config.authorization_services = {dt::Authorization::EIM};

        message_20::SessionStopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.charging_session = dt::ChargingSession::Terminate;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);

            const auto response_message = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_stop_res = response_message.value();
            REQUIRE(session_stop_res.response_code == dt::ResponseCode::OK);
        }
    }
    GIVEN("Sequence Error") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::Authorization>()};

        // Setting up session_config based on test
        ctx.session_config.cert_install_service = false;
        ctx.session_config.authorization_services = {dt::Authorization::EIM};

        message_20::SessionSetupRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.evccid = "WMIV1234567890ABCDEX";

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);

            const auto response_message = ctx.get_response<message_20::SessionSetupResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_setup_res = response_message.value();
            REQUIRE(session_setup_res.response_code == dt::ResponseCode::FAILED_SequenceError);
        }
    }

    // TODO(SL): Missing PnC test cases
}
