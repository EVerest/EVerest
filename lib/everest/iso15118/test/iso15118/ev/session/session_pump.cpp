// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

#include <arpa/inet.h>

#include <cbv2g/exi_v2gtp.h>

#include <everest/io/event/fd_event_handler.hpp>

#include <iso15118/io/sdp.hpp>
#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/variant.hpp>

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/session.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/session/logger.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using namespace iso15118::ev::test;

SCENARIO("EV Session holds each request and transmits it after the send delay") {

    GIVEN("A Session bound to a reactor with short send-delay and watchdog timers") {

        // Install a no-op session log callback so SessionLogger::event() does not throw
        // bad_function_call when a state's enter() invokes log.enter_state(...). evio's
        // real states all log on entry, so the pump test must provide a sink.
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});

        everest::lib::io::event::fd_event_handler reactor;

        std::vector<std::vector<uint8_t>> captured;
        std::optional<message_20::SessionSetupResponse> captured_setup_response;
        bool timed_out = false;

        ev::feedback::Callbacks callbacks{};
        callbacks.session_setup_response = [&captured_setup_response](const message_20::SessionSetupResponse& res) {
            captured_setup_response = res;
        };
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

        WHEN("start() is called") {
            session.start();

            THEN("Nothing is transmitted synchronously: the SAP request is held for the send delay") {
                REQUIRE(captured.empty());
            }

            THEN("The SAP request is emitted once the send delay elapses") {
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 1; }, 1s));
                REQUIRE(captured.size() == 1);

                const auto& frame = captured.front();
                REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::SAP);

                auto variant = decode_frame(frame);
                REQUIRE(variant.get_if<message_20::SupportedAppProtocolRequest>() != nullptr);
            }

            WHEN("A SAP response arrives, the next request is held then sent after the delay") {
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 1; }, 1s));

                message_20::SupportedAppProtocolResponse sap_res{
                    message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1};
                session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::SAP, serialize_msg(sap_res)));

                THEN("The SessionSetupRequest is not sent synchronously but after the send delay") {
                    REQUIRE(captured.size() == 1);
                    REQUIRE(pump_until(
                        reactor, [&]() { return captured.size() >= 2; }, 1s));

                    const auto& frame = captured.back();
                    REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);

                    auto variant = decode_frame(frame);
                    REQUIRE(variant.get_if<message_20::SessionSetupRequest>() != nullptr);
                }

                WHEN("A SessionSetupResponse(OK_NewSessionEstablished) arrives") {
                    REQUIRE(pump_until(
                        reactor, [&]() { return captured.size() >= 2; }, 1s));

                    message_20::SessionSetupResponse setup_res{};
                    setup_res.response_code = message_20::datatypes::ResponseCode::OK_NewSessionEstablished;
                    // evio's real session_setup aborts on an all-zero returned session_id and an
                    // empty evseid, so a valid establishment must carry both.
                    setup_res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
                    setup_res.evseid = "DE*PNX*E12345";
                    session.on_bytes_received(
                        frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(setup_res)));

                    THEN("An AuthorizationSetupRequest is emitted and the session keeps running") {
                        // evio's session_setup does NOT fire the session_setup_response feedback.
                        REQUIRE_FALSE(captured_setup_response.has_value());

                        REQUIRE(pump_until(
                            reactor, [&]() { return captured.size() >= 3; }, 1s));

                        const auto& frame = captured.back();
                        REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);

                        auto variant = decode_frame(frame);
                        REQUIRE(variant.get_if<message_20::AuthorizationSetupRequest>() != nullptr);

                        REQUIRE_FALSE(session.is_finished());
                        REQUIRE_FALSE(timed_out);
                    }
                }
            }
        }

        WHEN("start() runs but no response ever arrives") {
            session.start();
            REQUIRE(pump_until(
                reactor, [&]() { return captured.size() >= 1; }, 1s));

            THEN("The response watchdog fires: timed_out is signalled and the session finishes") {
                REQUIRE(pump_until(
                    reactor, [&]() { return session.is_finished(); }, 2s));
                REQUIRE(timed_out);
                REQUIRE(session.is_finished());
            }
        }

        WHEN("start() runs and a frame is split across two on_bytes_received calls") {
            session.start();
            REQUIRE(pump_until(
                reactor, [&]() { return captured.size() >= 1; }, 1s));

            message_20::SupportedAppProtocolResponse sap_res{
                message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1};
            const auto frame = frame_payload(io::v2gtp::PayloadType::SAP, serialize_msg(sap_res));

            const auto split = frame.size() / 2;
            session.on_bytes_received(std::vector<uint8_t>(frame.begin(), frame.begin() + split));

            THEN("No new request is produced until the rest of the frame arrives") {
                REQUIRE(captured.size() == 1);

                session.on_bytes_received(std::vector<uint8_t>(frame.begin() + split, frame.end()));

                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 2; }, 1s));
                REQUIRE(header_payload_type(captured.back()) == io::v2gtp::PayloadType::Part20Main);
            }
        }

        WHEN("start() runs and two complete frames arrive in a single on_bytes_received call") {
            // Under TCP coalescing the data path may hand the pump several whole frames
            // at once. The accumulator's while-loop must drain them all (with a reset
            // between frames), not just the first.
            session.start();
            REQUIRE(pump_until(
                reactor, [&]() { return captured.size() >= 1; }, 1s));

            message_20::SupportedAppProtocolResponse sap_res{
                message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1};
            const auto sap_frame = frame_payload(io::v2gtp::PayloadType::SAP, serialize_msg(sap_res));

            // The SessionSetup response would be the next frame the peer sends; concatenate
            // it after the SAP response so both arrive in one read.
            message_20::SessionSetupResponse setup_res{};
            setup_res.response_code = message_20::datatypes::ResponseCode::OK_NewSessionEstablished;
            setup_res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
            setup_res.evseid = "DE*PNX*E12345";
            const auto setup_frame = frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(setup_res));

            std::vector<uint8_t> coalesced;
            coalesced.insert(coalesced.end(), sap_frame.begin(), sap_frame.end());
            coalesced.insert(coalesced.end(), setup_frame.begin(), setup_frame.end());

            session.on_bytes_received(coalesced);

            THEN("Both frames are consumed and the FSM advances to AuthorizationSetupRequest") {
                // The SAP response advances to SessionSetup and the SessionSetup response
                // advances to AuthorizationSetup; the final request transmits after the delay.
                REQUIRE(pump_until(
                    reactor, [&]() { return captured.size() >= 2; }, 1s));

                const auto& frame = captured.back();
                REQUIRE(header_payload_type(frame) == io::v2gtp::PayloadType::Part20Main);

                auto variant = decode_frame(frame);
                REQUIRE(variant.get_if<message_20::AuthorizationSetupRequest>() != nullptr);

                REQUIRE_FALSE(session.is_finished());
                REQUIRE_FALSE(timed_out);
            }
        }

        WHEN("a frame with a malformed V2GTP header arrives") {
            // A peer sending garbage (here a bad SDP version byte) drives the accumulator
            // into a terminal INVALID_HEADER state. The pump must stop loudly rather than
            // silently dropping the byte stream and waiting for the watchdog.
            session.start();
            REQUIRE(pump_until(
                reactor, [&]() { return captured.size() >= 1; }, 1s));

            // Valid header is {0x01, 0xFE, ...}; corrupt the version so parse_header
            // rejects it. Payload bytes are irrelevant once the header is invalid.
            std::vector<uint8_t> bad_frame(io::SdpPacket::V2GTP_HEADER_SIZE, 0x00);
            bad_frame[0] = 0xFF; // not SDP_PROTOCOL_VERSION
            bad_frame[1] = 0xFF; // not SDP_INVERSE_PROTOCOL_VERSION

            session.on_bytes_received(bad_frame);

            THEN("The session stops and finishes instead of hanging until the watchdog") {
                REQUIRE(session.is_finished());
                // No second request was transmitted: the malformed frame produced nothing.
                REQUIRE(captured.size() == 1);
                // The failure was reported as a stop, not masqueraded as a timeout.
                REQUIRE_FALSE(timed_out);
            }
        }

        WHEN("a frame with a valid header but a garbage EXI payload arrives") {
            // Unlike the malformed-header case, the V2GTP header parses and the declared
            // length matches, so the accumulator completes the frame and hands it to the
            // decoder. The EXI payload is garbage: the Variant decode fails and yields a
            // Type::None variant, which the active state (SupportedAppProtocol) rejects as
            // the wrong variant and stops the session. Either way the pump must stop
            // loudly the same reactor pass rather than hanging until the response watchdog.
            session.start();
            REQUIRE(pump_until(
                reactor, [&]() { return captured.size() >= 1; }, 1s));

            // A well-formed 4-byte payload framed for a real payload type, but the bytes
            // are not a valid EXI document for it: decode fails inside handle_complete_frame.
            const std::vector<uint8_t> garbage_payload{0xde, 0xad, 0xbe, 0xef};
            session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, garbage_payload));

            THEN("The session stops synchronously, not via the response watchdog") {
                REQUIRE(session.is_finished());
                // No further request was produced from the undecodable frame.
                REQUIRE(captured.size() == 1);
                // Stopped on the decode/dispatch path, not masqueraded as a response timeout.
                REQUIRE_FALSE(timed_out);
            }
        }
    }

    GIVEN("A Session whose evcc_id overflows the SessionSetupRequest encode buffer") {
        // An over-long evccid (> the cb EVCCID buffer) makes the SessionSetupRequest
        // serializer throw; MessageExchange::take_request catches it and returns an
        // empty payload. The pump's encode-failure path must stop the session loudly
        // rather than transmit a truncated frame or hang.
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});

        everest::lib::io::event::fd_event_handler reactor;

        std::vector<std::vector<uint8_t>> captured;
        bool timed_out = false;

        ev::feedback::Callbacks callbacks{};
        callbacks.timed_out = [&timed_out]() { timed_out = true; };

        session::SessionLogger logger{nullptr};

        const ev::SessionTiming timing{5ms, 100ms};

        // 300 chars exceeds the 255-char EVCCID buffer -> CPP2CB_STRING throws.
        const std::string oversized_evcc_id(300, 'A');

        ev::Session session{callbacks,
                            [&captured](std::vector<uint8_t> frame) {
                                captured.push_back(std::move(frame));
                                return true;
                            },
                            logger,
                            reactor,
                            timing,
                            oversized_evcc_id};
        WHEN("the SAP exchange succeeds and the SessionSetupRequest fails to encode") {
            session.start();
            // The SAP request encodes fine (no evccid); transmit it.
            REQUIRE(pump_until(
                reactor, [&]() { return captured.size() >= 1; }, 1s));

            message_20::SupportedAppProtocolResponse sap_res{
                message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1};
            session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::SAP, serialize_msg(sap_res)));

            THEN("Nothing further is transmitted, the session stops and finishes, no watchdog fires") {
                REQUIRE(pump_until(
                    reactor, [&]() { return session.is_finished(); }, 1s));
                REQUIRE(session.is_finished());
                // Only the SAP request was ever sent; the un-encodable SessionSetupRequest
                // was dropped, not transmitted truncated.
                REQUIRE(captured.size() == 1);
                // Stopped via the encode-failure path, not the response watchdog.
                REQUIRE_FALSE(timed_out);
            }
        }
    }

    GIVEN("A Session that receives control events around its lifecycle") {
        // deliver_control_event must be safe to call before start() (no FSM yet) and
        // around it; the only reachable state (SupportedAppProtocol) reacts to
        // V2GTP_MESSAGE only, so a CONTROL_MESSAGE emits nothing through the real FSM.
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});

        everest::lib::io::event::fd_event_handler reactor;

        std::vector<std::vector<uint8_t>> captured;

        ev::feedback::Callbacks callbacks{};

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
        WHEN("a control event is delivered before start()") {
            // No FSM exists yet; this must be a no-op, not a crash.
            session.deliver_control_event(ev::d20::StopCharging{true});

            THEN("Nothing is transmitted and the session is not finished") {
                REQUIRE(captured.empty());
                REQUIRE_FALSE(session.is_finished());
            }
        }

        WHEN("a control event is delivered after start()") {
            session.start();
            REQUIRE(pump_until(
                reactor, [&]() { return captured.size() >= 1; }, 1s));
            const auto count_after_sap = captured.size();

            session.deliver_control_event(ev::d20::StopCharging{true});

            THEN("The SupportedAppProtocol state ignores it: no extra frame is produced") {
                // Give the reactor a chance to flush any (erroneously) produced request.
                pump_until(
                    reactor, [&]() { return captured.size() > count_after_sap; }, 100ms);
                REQUIRE(captured.size() == count_after_sap);
            }
        }
    }

    GIVEN("A Session whose timed_out feedback throws on first invocation") {
        // timed_out() invokes a consumer-supplied std::function, which can throw. The
        // watchdog handler must signal timed_out EXACTLY ONCE: if it fired the callback
        // inside the FSM-feed try AND again in the catch, a throwing callback would be
        // invoked twice and the catch's re-throw would escape into the reactor's
        // unguarded poll loop. Pin one-shot delivery.
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});

        everest::lib::io::event::fd_event_handler reactor;

        std::vector<std::vector<uint8_t>> captured;
        int timed_out_count = 0;

        ev::feedback::Callbacks callbacks{};
        callbacks.timed_out = [&timed_out_count]() {
            ++timed_out_count;
            // Throw on the first call: the buggy double-fire structure would re-enter
            // the callback from its catch block, bumping the count to 2.
            throw std::runtime_error("consumer timed_out callback failure");
        };

        session::SessionLogger logger{nullptr};

        const ev::SessionTiming timing{5ms, 50ms};

        ev::Session session{callbacks,
                            [&captured](std::vector<uint8_t> frame) {
                                captured.push_back(std::move(frame));
                                return true;
                            },
                            logger,
                            reactor,
                            timing,
                            "EVTESTID01"};

        WHEN("start() runs and the response watchdog fires") {
            session.start();

            // Pump the SAP request out (which arms the watchdog), then keep polling so
            // the watchdog expires. The throwing callback escapes the reactor's
            // poll_impl, so guard the pump and stop once timed_out has been invoked.
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (timed_out_count == 0 and std::chrono::steady_clock::now() < deadline) {
                try {
                    reactor.poll(1ms);
                    reactor.run_actions();
                } catch (const std::exception&) {
                    // The watchdog fired and the consumer callback threw; that escape is
                    // the documented behavior. Stop pumping and assert the call count.
                    break;
                }
            }

            THEN("timed_out is invoked exactly once and the session is stopped") {
                REQUIRE(timed_out_count == 1);
                // stop_session(true) ran before the callback, so the session is finished
                // regardless of the throw.
                REQUIRE(session.is_finished());
            }
        }
    }
}

SCENARIO("EV Session stops loudly when the outbound send is refused") {
    // The outbound seam reports its result. A refused send leaves no frame on the wire,
    // so arming the response watchdog would only masquerade the failure as a timeout.
    // transmit_pending must stop the session loudly instead.
    iso15118::session::logging::set_session_log_callback([](std::size_t, const iso15118::session::logging::Event&) {});

    everest::lib::io::event::fd_event_handler reactor;

    int send_attempts = 0;
    bool timed_out = false;

    ev::feedback::Callbacks callbacks{};
    callbacks.timed_out = [&timed_out]() { timed_out = true; };

    session::SessionLogger logger{nullptr};

    const ev::SessionTiming timing{5ms, 200ms};

    // Refuse every send.
    ev::Session session{callbacks,
                        [&send_attempts](std::vector<uint8_t>) {
                            ++send_attempts;
                            return false;
                        },
                        logger,
                        reactor,
                        timing,
                        "EVTESTID01"};
    GIVEN("A Session whose outbound seam refuses the SAP request") {
        session.start();

        WHEN("the send-delay elapses and the first frame is refused") {
            THEN("The session stops loudly the same pass, not via the response watchdog") {
                REQUIRE(pump_until(
                    reactor, [&]() { return session.is_finished(); }, 1s));
                // The send was attempted exactly once and refused.
                REQUIRE(send_attempts == 1);
                // Stopped via the send-failure path, not the 200ms watchdog.
                REQUIRE_FALSE(timed_out);
            }
        }
    }
}

SCENARIO("EV Session stops loudly when a state consumes a response without acting") {
    // The Authorization stub's feed() returns {} on V2GTP_MESSAGE: it neither produces
    // a follow-up request nor stops the session. handle_complete_frame disarms the
    // watchdog before feeding, so a state that consumes the response without acting
    // would leave NO timer armed -> a silent hang. The pump must detect this and stop
    // loudly the same reactor pass, NOT wait out the watchdog.
    iso15118::session::logging::set_session_log_callback([](std::size_t, const iso15118::session::logging::Event&) {});

    everest::lib::io::event::fd_event_handler reactor;

    std::vector<std::vector<uint8_t>> captured;
    bool timed_out = false;

    ev::feedback::Callbacks callbacks{};
    callbacks.timed_out = [&timed_out]() { timed_out = true; };

    session::SessionLogger logger{nullptr};

    const ev::SessionTiming timing{5ms, 200ms};

    ev::Session session{callbacks,
                        [&captured](std::vector<uint8_t> frame) {
                            captured.push_back(std::move(frame));
                            return true;
                        },
                        logger,
                        reactor,
                        timing,
                        "EVTESTID01"};
    GIVEN("A Session walked to the Authorization state") {
        const message_20::datatypes::SessionId session_id{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

        session.start();
        REQUIRE(pump_until(
            reactor, [&]() { return captured.size() >= 1; }, 1s)); // SAP request

        message_20::SupportedAppProtocolResponse sap_res{
            message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1};
        session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::SAP, serialize_msg(sap_res)));
        REQUIRE(pump_until(
            reactor, [&]() { return captured.size() >= 2; }, 1s)); // SessionSetup request

        message_20::SessionSetupResponse setup_res{};
        setup_res.response_code = message_20::datatypes::ResponseCode::OK_NewSessionEstablished;
        setup_res.header.session_id = session_id;
        setup_res.evseid = "DE*PNX*E12345";
        session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(setup_res)));
        REQUIRE(pump_until(
            reactor, [&]() { return captured.size() >= 3; }, 1s)); // AuthorizationSetup request

        message_20::AuthorizationSetupResponse auth_setup_res{};
        auth_setup_res.header.session_id = session_id;
        auth_setup_res.response_code = message_20::datatypes::ResponseCode::OK;
        auth_setup_res.authorization_services = {message_20::datatypes::Authorization::EIM};
        auth_setup_res.certificate_installation_service = false;
        auth_setup_res.authorization_mode = message_20::datatypes::EIM_ASResAuthorizationMode{};
        session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(auth_setup_res)));
        REQUIRE(pump_until(
            reactor, [&]() { return captured.size() >= 4; }, 1s)); // Authorization request

        WHEN("an AuthorizationResponse is injected and the stub consumes it without acting") {
            message_20::AuthorizationResponse auth_res{};
            auth_res.header.session_id = session_id;
            auth_res.response_code = message_20::datatypes::ResponseCode::OK;
            auth_res.evse_processing = message_20::datatypes::Processing::Finished;
            session.on_bytes_received(frame_payload(io::v2gtp::PayloadType::Part20Main, serialize_msg(auth_res)));

            THEN("The session stops loudly the same pass, not via the response watchdog") {
                REQUIRE(session.is_finished());
                // No fifth request was produced (the stub emitted nothing).
                REQUIRE(captured.size() == 4);
                // Stopped via the no-op-state guard, not the 200ms watchdog.
                REQUIRE_FALSE(timed_out);
            }
        }
    }
}
