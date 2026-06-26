// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Pump-level FSM-walk integration test.
//
// This drives an ev::Session purely by injecting canned, EXI-encoded V2GTP
// response frames via session.on_bytes_received(...) and asserting the request
// frame the pump emits at each step. No fsm.feed() and no manual create_state<>()
// are used: the only input is bytes, the only output is captured frames. This is
// the proof that fsm::v2::FSM<d20::StateBase> drives evio's real EV states
// unchanged through the implemented entry sequence.
//
// Achievable clean walk (each arrow = one injected response → one emitted request):
//
//   start()                         -> SupportedAppProtocolRequest
//   SupportedAppProtocolResponse    -> SessionSetupRequest
//   SessionSetupResponse            -> AuthorizationSetupRequest
//   AuthorizationSetupResponse      -> AuthorizationRequest
//
// i.e. SupportedAppProtocol -> SessionSetup -> AuthorizationSetup -> Authorization.
//
// WHY THE WALK STOPS AT Authorization (intentional boundary, not a defect):
//   1. Deferred breadth: the EV-side Authorization
//      (state/authorization.cpp), DC_ChargeParameterDiscovery, and PowerDelivery
//      states are unimplemented. Authorization::feed() returns {} on every event,
//      so once the FSM is in Authorization it cannot self-advance from injected
//      bytes alone. Driving further would require code that does not yet exist.
//   2. Two user-approved PRE-EXISTING reds (single-slot MessageExchange + eager
//      enter overwrite, NOT this plan's bug) block DC_PreCharge -> PowerDelivery
//      and DC_WeldingDetection -> SessionStop. Those stay red by agreement.
//
// Therefore the test asserts exactly three byte-driven transitions and stops at
// Authorization. It deliberately does NOT assert any transition the code cannot
// make. Once parked in Authorization with no further response injected, the only
// thing that ends the walk is the response watchdog (it feeds FAILED, which
// Authorization::feed also ignores, then stops the session and fires timed_out);
// no fifth request frame is ever emitted from bytes alone.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

#include <arpa/inet.h>

#include <cbv2g/exi_v2gtp.h>

#include <everest/io/event/fd_event_handler.hpp>

#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/variant.hpp>

#include <iso15118/ev/session.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/session/logger.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using namespace iso15118::ev::test;

SCENARIO("EV pump drives evio states byte-by-byte to the Authorization boundary") {

    GIVEN("A Session bound to a reactor with short send-delay and watchdog timers") {

        // evio's real states log on entry; provide a no-op sink so SessionLogger
        // does not throw bad_function_call from a state's enter().
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});

        everest::lib::io::event::fd_event_handler reactor;

        std::vector<std::vector<uint8_t>> captured;
        bool timed_out = false;

        ev::feedback::Callbacks callbacks{};
        callbacks.timed_out = [&timed_out]() { timed_out = true; };

        session::SessionLogger logger{nullptr};

        const ev::SessionTiming timing{5ms, 100ms};

        ev::Session session{callbacks,
                            [&captured](std::vector<uint8_t> frame) {
                                captured.push_back(std::move(frame));
                                return true;
                            },
                            logger,
                            reactor,
                            timing,
                            "EVTESTID01"};

        WHEN("the session starts and is fed SAP, SessionSetup, then AuthorizationSetup responses") {

            // --- start() -> SupportedAppProtocolRequest ---
            session.start();
            REQUIRE(pump_until(
                reactor, [&]() { return captured.size() >= 1; }, 1s));
            {
                const auto& frame = captured.back();
                REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::SAP);
                auto variant = decode_frame(frame);
                REQUIRE(variant.get_if<message_20::SupportedAppProtocolRequest>() != nullptr);
            }

            // --- inject SupportedAppProtocolResponse -> SessionSetupRequest ---
            {
                message_20::SupportedAppProtocolResponse sap_res{
                    message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1};
                session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::SAP, serialize_msg(sap_res)));
            }
            REQUIRE(pump_until(
                reactor, [&]() { return captured.size() >= 2; }, 1s));
            {
                const auto& frame = captured.back();
                REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);
                auto variant = decode_frame(frame);
                REQUIRE(variant.get_if<message_20::SessionSetupRequest>() != nullptr);
            }

            // --- inject SessionSetupResponse(OK_NewSessionEstablished) -> AuthorizationSetupRequest ---
            // evio's session_setup aborts on an all-zero returned session_id or an empty
            // evseid, so a valid establishment must carry both.
            const message_20::datatypes::SessionId session_id{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
            {
                message_20::SessionSetupResponse setup_res{};
                setup_res.response_code = message_20::datatypes::ResponseCode::OK_NewSessionEstablished;
                setup_res.header.session_id = session_id;
                setup_res.evseid = "DE*PNX*E12345";
                session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(setup_res)));
            }
            REQUIRE(pump_until(
                reactor, [&]() { return captured.size() >= 3; }, 1s));
            {
                const auto& frame = captured.back();
                REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);
                auto variant = decode_frame(frame);
                REQUIRE(variant.get_if<message_20::AuthorizationSetupRequest>() != nullptr);
            }

            // --- inject AuthorizationSetupResponse(OK, {EIM}) -> AuthorizationRequest ---
            // This is the final byte-driven transition: AuthorizationSetup selects EIM,
            // emits an EIM AuthorizationRequest, and transitions into the Authorization
            // state. The walk stops here (see file header for the boundary rationale).
            {
                message_20::AuthorizationSetupResponse auth_setup_res{};
                auth_setup_res.header.session_id = session_id;
                auth_setup_res.response_code = message_20::datatypes::ResponseCode::OK;
                auth_setup_res.authorization_services = {message_20::datatypes::Authorization::EIM};
                auth_setup_res.certificate_installation_service = false;
                auth_setup_res.authorization_mode = message_20::datatypes::EIM_ASResAuthorizationMode{};
                session.on_bytes_received(
                    frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(auth_setup_res)));
            }

            THEN("the pump emits an EIM AuthorizationRequest and reaches the Authorization boundary") {
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 4; }, 1s));

                const auto& frame = captured.back();
                REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);

                auto variant = decode_frame(frame);
                const auto* request = variant.get_if<message_20::AuthorizationRequest>();
                REQUIRE(request != nullptr);

                // The request carries the established session id and selects EIM.
                REQUIRE(request->header.session_id == session_id);
                REQUIRE(request->selected_authorization_service == message_20::datatypes::Authorization::EIM);
                REQUIRE(std::holds_alternative<message_20::datatypes::EIM_ASReqAuthorizationMode>(
                    request->authorization_mode));

                // The session is still live: it walked three byte-driven transitions
                // without finishing or timing out.
                REQUIRE_FALSE(session.is_finished());
                REQUIRE_FALSE(timed_out);
            }

            THEN("no further request is produced past Authorization from injected bytes alone") {
                // Reach the Authorization boundary first (the AuthorizationRequest is the
                // fourth and final byte-driven frame).
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 4; }, 1s));
                const auto count_at_boundary = captured.size();
                REQUIRE(count_at_boundary == 4);

                // Authorization::feed() returns {} on every event (deferred breadth), so the
                // FSM cannot self-advance from here. With no AuthorizationResponse injected,
                // the only thing that can terminate the walk is the response watchdog
                // (response_timeout = 100ms): it feeds FAILED, which Authorization::feed also
                // ignores, then stops the session and fires timed_out. No fifth request frame
                // is ever emitted. Pump past the watchdog deadline to prove exactly that.
                REQUIRE(pump_until(
                    reactor, [&]() { return session.is_finished(); }, 1s));
                REQUIRE(captured.size() == count_at_boundary); // no fifth request from bytes
                REQUIRE(timed_out);                            // terminated by the watchdog, not a transition
            }
        }
    }
}
