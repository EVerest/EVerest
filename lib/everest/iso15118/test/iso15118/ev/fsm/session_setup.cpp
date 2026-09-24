// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/secc_clock.hpp>
#include <iso15118/ev/d20/state/session_setup.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 EV session setup state transitions") {

    const ev::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(callbacks);

    auto& ctx = state_helper.get_context();

    GIVEN("Good case - new session") {
        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::SessionSetup>()};

        const auto header = message_20::Header{{0x10, 0x34, 0xAB, 0x7A, 0x01, 0xF3, 0x95, 0x02}, 1691411798};

        const auto res = message_20::SessionSetupResponse{
            header, message_20::datatypes::ResponseCode::OK_NewSessionEstablished, "everest se"};

        state_helper.handle_response(res);
        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("Check if passes to authorization setup state and sends AuthorizationSetupRequest") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::AuthorizationSetup);

            const auto requests = take_all_requests(state_helper.get_message_exchange());
            const auto request_message = requests.get<message_20::AuthorizationSetupRequest>();
            REQUIRE(request_message.has_value());

            const auto& authorization_setup_req = request_message.value();
            REQUIRE(authorization_setup_req.header.session_id == ctx.get_session().get_id());
        }
    }
}

// This EV exists to surface SECC deviations, so SessionSetup is strict: a new session
// is only accepted on OK_NewSessionEstablished. Plain OK and every WARNING_* stop the
// session here even though the generic response-code table accepts them elsewhere.
SCENARIO("ISO15118-20 EV session setup rejects every code but OK_NewSessionEstablished") {

    const ev::feedback::Callbacks callbacks{};

    const auto expect_rejected = [&](message_20::datatypes::ResponseCode code) {
        auto state_helper = FsmStateHelper(callbacks);
        auto& ctx = state_helper.get_context();

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::SessionSetup>()};

        const auto res = message_20::SessionSetupResponse{SESSION_HEADER, code, "everest se"};

        state_helper.handle_response(res);
        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("the session is stopped without adopting the id") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::SessionSetup);
            REQUIRE(ctx.is_session_stopped() == true);
            REQUIRE(ctx.get_session().get_id() == message_20::datatypes::SessionId{});
            REQUIRE(take_all_requests(state_helper.get_message_exchange()).empty());
        }
    };

    GIVEN("a plain OK response carrying a non-zero session id") {
        expect_rejected(message_20::datatypes::ResponseCode::OK);
    }

    GIVEN("an OK_CertificateExpiresSoon response carrying a non-zero session id") {
        expect_rejected(message_20::datatypes::ResponseCode::OK_CertificateExpiresSoon);
    }

    GIVEN("a WARNING_EIMAuthorizationFailure response carrying a non-zero session id") {
        expect_rejected(message_20::datatypes::ResponseCode::WARNING_EIMAuthorizationFailure);
    }

    GIVEN("an OK_NewSessionEstablished response carrying a zero session id") {
        auto state_helper = FsmStateHelper(callbacks);
        auto& ctx = state_helper.get_context();

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::SessionSetup>()};

        const auto zero_header = message_20::Header{std::array<uint8_t, 8>{}, 1691411798};
        const auto res = message_20::SessionSetupResponse{
            zero_header, message_20::datatypes::ResponseCode::OK_NewSessionEstablished, "everest se"};

        state_helper.handle_response(res);
        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        THEN("the session is stopped rather than continued with an unusable id") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::SessionSetup);
            REQUIRE(ctx.is_session_stopped() == true);
            REQUIRE(take_all_requests(state_helper.get_message_exchange()).empty());
        }
    }
}

// The EV never requests a resume, so OK_OldSessionJoined is a protocol error regardless
// of the selected service. Characterization test: the deleted resume path also stopped the
// session here, so this pins the explicit rejection rather than distinguishing it.
SCENARIO("ISO15118-20 EV session setup rejects a resumed session") {

    const ev::feedback::Callbacks callbacks{};

    const auto run = [&](message_20::datatypes::ServiceCategory service) {
        auto state_helper = FsmStateHelper(callbacks, DEFAULT_APP_PROTOCOLS, service);
        auto& ctx = state_helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);

        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::SessionSetup>()};

        const auto res = message_20::SessionSetupResponse{
            SESSION_HEADER, message_20::datatypes::ResponseCode::OK_OldSessionJoined, "everest se"};

        state_helper.handle_response(res);
        const auto result = fsm.feed(ev::d20::Event::V2GTP_MESSAGE);

        REQUIRE(result.transitioned() == false);
        REQUIRE(result.output == ev::d20::Disposition::Stopping);
        REQUIRE(fsm.get_current_state_id() == ev::d20::StateID::SessionSetup);
        REQUIRE(ctx.is_session_stopped() == true);
        REQUIRE(take_all_requests(state_helper.get_message_exchange()).empty());
    };

    GIVEN("an AC selected service") {
        run(message_20::datatypes::ServiceCategory::AC);
    }

    GIVEN("a DC selected service") {
        run(message_20::datatypes::ServiceCategory::DC);
    }
}

SCENARIO("ISO15118-20 EV request headers carry SECC time in microseconds [V2G20-1534]") {

    const ev::feedback::Callbacks callbacks{};

    const auto first_header_after_setup = [&](std::uint64_t reference) {
        auto state_helper = FsmStateHelper(callbacks);
        auto& ctx = state_helper.get_context();

        const auto before = ev::d20::SeccClock::system_now_us();
        fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::SessionSetup>()};
        const auto after = ev::d20::SeccClock::system_now_us();

        const auto setup_req =
            take_all_requests(state_helper.get_message_exchange()).get<message_20::SessionSetupRequest>();
        REQUIRE(setup_req.has_value());
        // [V2G20-1535]: the local UTC guess.
        REQUIRE(setup_req->header.timestamp >= before);
        REQUIRE(setup_req->header.timestamp <= after);

        const auto received = std::chrono::steady_clock::now();
        state_helper.handle_response(message_20::SessionSetupResponse{
            message_20::Header{SESSION_HEADER.session_id, reference},
            message_20::datatypes::ResponseCode::OK_NewSessionEstablished, "everest se"});
        REQUIRE(fsm.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());

        const auto auth_setup_req =
            take_all_requests(state_helper.get_message_exchange()).get<message_20::AuthorizationSetupRequest>();
        REQUIRE(auth_setup_req.has_value());
        REQUIRE(auth_setup_req->header.session_id == SESSION_HEADER.session_id);
        // [V2G20-1536]
        require_tracks_reference(auth_setup_req->header.timestamp, reference, received);
        REQUIRE(auth_setup_req->header.timestamp > reference);
    };

    GIVEN("a new session whose SECC time is far below local UTC") {
        first_header_after_setup(SECC_REFERENCE_US);
    }

    GIVEN("a new session whose SECC time is far above local UTC") {
        first_header_after_setup(ev::d20::SeccClock::system_now_us() * 2);
    }
}

SCENARIO("ISO15118-20 EV request headers keep increasing across a resume [V2G20-1537]") {

    const ev::feedback::Callbacks callbacks{};

    // The paused session, synchronized to an SECC epoch at power-on.
    auto paused = FsmStateHelper(callbacks);
    paused.get_context().secc_clock().synchronize(SECC_REFERENCE_US);
    paused.get_context().send_request(message_20::AuthorizationSetupRequest{});
    const auto last = take_all_requests(paused.get_message_exchange()).get<message_20::AuthorizationSetupRequest>();
    REQUIRE(last.has_value());

    ev::d20::SessionOptions options{};
    options.resumed_session_id = SESSION_HEADER.session_id;
    options.secc_clock = paused.get_context().secc_clock().state();
    auto state_helper = FsmStateHelper(callbacks, DEFAULT_APP_PROTOCOLS, message_20::datatypes::ServiceCategory::DC,
                                       std::move(options));
    auto& ctx = state_helper.get_context();

    fsm::v2::FSM<ev::d20::StateBase> fsm{ctx.create_state<ev::d20::state::SessionSetup>()};
    const auto setup_req =
        take_all_requests(state_helper.get_message_exchange()).get<message_20::SessionSetupRequest>();
    REQUIRE(setup_req.has_value());
    // Already in SECC time, not the local UTC guess.
    REQUIRE(setup_req->header.timestamp > last->header.timestamp);
    REQUIRE(setup_req->header.timestamp < ev::d20::SeccClock::system_now_us() / 2);

    // An SECC reference no later than the stamps already sent: they still do not go back.
    state_helper.handle_response(
        message_20::SessionSetupResponse{message_20::Header{SESSION_HEADER.session_id, SECC_REFERENCE_US},
                                         message_20::datatypes::ResponseCode::OK_OldSessionJoined, "everest se"});
    REQUIRE(fsm.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());

    const auto auth_setup_req =
        take_all_requests(state_helper.get_message_exchange()).get<message_20::AuthorizationSetupRequest>();
    REQUIRE(auth_setup_req.has_value());
    REQUIRE(auth_setup_req->header.timestamp > setup_req->header.timestamp);
}
