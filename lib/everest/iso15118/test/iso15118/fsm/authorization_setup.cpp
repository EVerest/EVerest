// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/authorization.hpp>
#include <iso15118/d20/state/authorization_setup.hpp>

#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 authorization setup state transitions") {

    const d20::EvseSetupConfig evse_setup = create_default_evse_setup();

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};

    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();
    ctx.session = d20::Session();

    GIVEN("Bad Case - Unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};

        // Setting up session_config based on test
        ctx.session_config.cert_install_service = false;
        ctx.session_config.authorization_services = {dt::Authorization::EIM};

        message_20::AuthorizationSetupRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AuthorizationSetup);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::AuthorizationSetupResponse>();
            REQUIRE(response_message.has_value());

            const auto& authorization_setup_res = response_message.value();

            REQUIRE(authorization_setup_res.response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(authorization_setup_res.certificate_installation_service == false);
            REQUIRE(authorization_setup_res.authorization_services.size() == 1);
            REQUIRE(authorization_setup_res.authorization_services[0] == dt::Authorization::EIM);
            REQUIRE(std::holds_alternative<dt::EIM_ASResAuthorizationMode>(authorization_setup_res.authorization_mode));
        }
    }

    GIVEN("Good Case - EIM only , cert_install_service not provided") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};

        // Setting up session_config based on test
        ctx.session_config.cert_install_service = false;
        ctx.session_config.authorization_services = {dt::Authorization::EIM};

        message_20::AuthorizationSetupRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);

            const auto response_message = ctx.get_response<message_20::AuthorizationSetupResponse>();
            REQUIRE(response_message.has_value());

            const auto& authorization_setup_res = response_message.value();

            REQUIRE(authorization_setup_res.response_code == dt::ResponseCode::OK);
            REQUIRE(authorization_setup_res.certificate_installation_service == false);
            REQUIRE(authorization_setup_res.authorization_services.size() == 1);
            REQUIRE(authorization_setup_res.authorization_services[0] == dt::Authorization::EIM);
            REQUIRE(std::holds_alternative<dt::EIM_ASResAuthorizationMode>(authorization_setup_res.authorization_mode));
            REQUIRE(ctx.session.offered_services.auth_services[0] == dt::Authorization::EIM);
        }
    }

    GIVEN("Good Case - PnC only, cert_install_service not provided") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};

        // Setting up session_config based on test
        ctx.session_config.cert_install_service = false;
        ctx.session_config.authorization_services = {dt::Authorization::PnC};

        message_20::AuthorizationSetupRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);

            const auto response_message = ctx.get_response<message_20::AuthorizationSetupResponse>();
            REQUIRE(response_message.has_value());

            const auto& authorization_setup_res = response_message.value();

            REQUIRE(authorization_setup_res.response_code == dt::ResponseCode::OK);
            REQUIRE(authorization_setup_res.certificate_installation_service == false);
            REQUIRE(authorization_setup_res.authorization_services.size() == 1);
            REQUIRE(authorization_setup_res.authorization_services[0] == dt::Authorization::PnC);
            REQUIRE(std::holds_alternative<dt::PnC_ASResAuthorizationMode>(authorization_setup_res.authorization_mode));
            const auto& auth_mode =
                std::get<dt::PnC_ASResAuthorizationMode>(authorization_setup_res.authorization_mode);
            REQUIRE(auth_mode.gen_challenge.empty() == false);
            REQUIRE(ctx.session.offered_services.auth_services[0] == dt::Authorization::PnC);
        }
    }

    GIVEN("Good Case - EIM + PnC, cert_install_service provided") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};

        // Setting up session_config based on test
        ctx.session_config.cert_install_service = true;
        ctx.session_config.authorization_services = {dt::Authorization::PnC, dt::Authorization::EIM};

        message_20::AuthorizationSetupRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::Authorization);

            const auto response_message = ctx.get_response<message_20::AuthorizationSetupResponse>();
            REQUIRE(response_message.has_value());

            const auto& authorization_setup_res = response_message.value();

            REQUIRE(authorization_setup_res.response_code == dt::ResponseCode::OK);
            REQUIRE(authorization_setup_res.certificate_installation_service == true);
            REQUIRE(authorization_setup_res.authorization_services.size() == 2);
            REQUIRE((authorization_setup_res.authorization_services[0] == dt::Authorization::EIM ||
                     authorization_setup_res.authorization_services[0] == dt::Authorization::PnC));
            REQUIRE((authorization_setup_res.authorization_services[1] == dt::Authorization::EIM ||
                     authorization_setup_res.authorization_services[1] == dt::Authorization::PnC));
            REQUIRE(std::holds_alternative<dt::PnC_ASResAuthorizationMode>(authorization_setup_res.authorization_mode));
            const auto& auth_mode =
                std::get<dt::PnC_ASResAuthorizationMode>(authorization_setup_res.authorization_mode);
            REQUIRE(auth_mode.gen_challenge.empty() == false);

            REQUIRE((ctx.session.offered_services.auth_services[0] == dt::Authorization::EIM ||
                     ctx.session.offered_services.auth_services[0] == dt::Authorization::PnC));
            REQUIRE((ctx.session.offered_services.auth_services[1] == dt::Authorization::EIM ||
                     ctx.session.offered_services.auth_services[1] == dt::Authorization::PnC));
        }
    }

    GIVEN("Event then other V2GTP_MESSAGE") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};
        const auto result = fsm.feed(d20::Event::CONTROL_MESSAGE);
        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AuthorizationSetup);
        }
    }
    GIVEN("SessionStopReq") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};

        // Setting up session_config based on test
        ctx.session_config.cert_install_service = false;
        ctx.session_config.authorization_services = {dt::Authorization::PnC};

        message_20::SessionStopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.charging_session = dt::ChargingSession::Terminate;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AuthorizationSetup);

            const auto response_message = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_stop_res = response_message.value();
            REQUIRE(session_stop_res.response_code == dt::ResponseCode::OK);
        }
    }
    GIVEN("Sequence Error") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AuthorizationSetup>()};

        // Setting up session_config based on test
        ctx.session_config.cert_install_service = false;
        ctx.session_config.authorization_services = {dt::Authorization::PnC};

        message_20::SessionSetupRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.evccid = "WMIV1234567890ABCDEX";

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AuthorizationSetup);

            const auto response_message = ctx.get_response<message_20::SessionSetupResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_setup_res = response_message.value();
            REQUIRE(session_setup_res.response_code == dt::ResponseCode::FAILED_SequenceError);
        }
    }
}
