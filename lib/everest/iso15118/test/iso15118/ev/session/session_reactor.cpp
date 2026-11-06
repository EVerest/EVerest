// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/variant.hpp>

#include <iso15118/ev/d20/control_event.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using namespace iso15118::ev::test;

namespace {
message_20::SupportedAppProtocolResponse ok_sap_response() {
    return {message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1};
}

message_20::SessionSetupResponse established_session_setup_response() {
    // The real session_setup aborts on an all-zero returned session_id or an empty
    // evseid, so a valid establishment must carry both.
    message_20::SessionSetupResponse res{};
    res.response_code = message_20::datatypes::ResponseCode::OK_NewSessionEstablished;
    res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    res.evseid = "DE*PNX*E12345";
    return res;
}

message_20::SessionStopResponse stop_response() {
    message_20::SessionStopResponse res{};
    res.header.session_id = established_session_setup_response().header.session_id;
    res.response_code = message_20::datatypes::ResponseCode::OK;
    return res;
}

} // namespace

SCENARIO("ISO15118-20 EV Session holds each request and transmits it after the send delay") {

    GIVEN("A Session bound to a reactor with short send-delay and watchdog timers") {
        SessionFixture fx;

        WHEN("start() is called") {
            fx.session.start();

            THEN("Nothing is transmitted synchronously: the SAP request is held for the send delay") {
                REQUIRE(fx.captured.empty());
            }

            THEN("The SAP request is emitted once the send delay elapses") {
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));
                REQUIRE(fx.captured.size() == 1);

                const auto& frame = fx.captured.front();
                REQUIRE(header_payload_type(frame) == v2gtp::PayloadType::SAP);

                auto variant = decode_frame(frame);
                REQUIRE(variant.get_if<message_20::SupportedAppProtocolRequest>() != nullptr);
            }

            WHEN("A SAP response arrives, the next request is held then sent after the delay") {
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));

                fx.session.on_bytes_received(frame_payload(v2gtp::PayloadType::SAP, serialize_msg(ok_sap_response())));

                THEN("The SessionSetupRequest is not sent synchronously but after the send delay") {
                    REQUIRE(fx.captured.size() == 1);
                    REQUIRE(run_reactor_until(
                        fx.reactor, [&]() { return fx.captured.size() >= 2; }, 1s));

                    const auto& frame = fx.captured.back();
                    REQUIRE(header_payload_type(frame) == v2gtp::PayloadType::Part20Main);

                    auto variant = decode_frame(frame);
                    REQUIRE(variant.get_if<message_20::SessionSetupRequest>() != nullptr);
                }

                WHEN("A SessionSetupResponse(OK_NewSessionEstablished) arrives") {
                    REQUIRE(run_reactor_until(
                        fx.reactor, [&]() { return fx.captured.size() >= 2; }, 1s));

                    fx.session.on_bytes_received(frame_payload(v2gtp::PayloadType::Part20Main,
                                                               serialize_msg(established_session_setup_response())));

                    THEN("An AuthorizationSetupRequest is emitted and the session keeps running") {
                        REQUIRE(run_reactor_until(
                            fx.reactor, [&]() { return fx.captured.size() >= 3; }, 1s));

                        const auto& frame = fx.captured.back();
                        REQUIRE(header_payload_type(frame) == v2gtp::PayloadType::Part20Main);

                        auto variant = decode_frame(frame);
                        REQUIRE(variant.get_if<message_20::AuthorizationSetupRequest>() != nullptr);

                        REQUIRE_FALSE(fx.session.is_finished());
                        REQUIRE_FALSE(fx.timed_out);
                    }
                }
            }
        }

        WHEN("start() runs but no response ever arrives") {
            fx.session.start();
            REQUIRE(run_reactor_until(
                fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));

            THEN("The response watchdog fires: timed_out is signalled and the session finishes") {
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.session.is_finished(); }, 2s));
                REQUIRE(fx.timed_out);
                REQUIRE(fx.session.is_finished());
            }
        }

        WHEN("start() runs and a frame is split across two on_bytes_received calls") {
            fx.session.start();
            REQUIRE(run_reactor_until(
                fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));

            const auto frame = frame_payload(v2gtp::PayloadType::SAP, serialize_msg(ok_sap_response()));

            const auto split = frame.size() / 2;
            fx.session.on_bytes_received(std::vector<uint8_t>(frame.begin(), frame.begin() + split));

            THEN("No new request is produced until the rest of the frame arrives") {
                REQUIRE(fx.captured.size() == 1);

                fx.session.on_bytes_received(std::vector<uint8_t>(frame.begin() + split, frame.end()));

                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.captured.size() >= 2; }, 1s));
                REQUIRE(header_payload_type(fx.captured.back()) == v2gtp::PayloadType::Part20Main);
            }
        }

        WHEN("a read carries a whole response frame plus the head of the next one") {
            // In-protocol coalescing: one read delivers the complete SAP response followed
            // by the head of the SessionSetup response. The accumulator's while-loop must
            // consume the complete SAP frame (advancing the FSM), reset, then buffer the
            // leftover head without losing or mis-parsing it. The SessionSetup frame only
            // completes once its tail arrives in the next read - after the EV has
            // transmitted its SessionSetupRequest - so the sequence stays in-protocol from
            // the FSM's view (the peer never answers before the request is on the wire).
            fx.session.start();
            REQUIRE(run_reactor_until(
                fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));

            const auto sap_frame = frame_payload(v2gtp::PayloadType::SAP, serialize_msg(ok_sap_response()));
            const auto setup_frame =
                frame_payload(v2gtp::PayloadType::Part20Main, serialize_msg(established_session_setup_response()));

            // Split the SessionSetup frame past its 8-byte header so the accumulator has to
            // buffer a parsed-header-but-incomplete-payload frame across the read boundary.
            const auto head = std::max<std::size_t>(setup_frame.size() / 2, io::SdpPacket::V2GTP_HEADER_SIZE + 1);
            REQUIRE(head < setup_frame.size());

            std::vector<uint8_t> first_read;
            first_read.insert(first_read.end(), sap_frame.begin(), sap_frame.end());
            first_read.insert(first_read.end(), setup_frame.begin(), setup_frame.begin() + head);
            fx.session.on_bytes_received(first_read);

            THEN("The SAP frame is consumed and the buffered head completes only on its tail") {
                // The consumed SAP response advanced the FSM to SessionSetup; its request
                // transmits after the send delay. The buffered SessionSetup head alone
                // produces nothing further.
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.captured.size() >= 2; }, 1s));
                REQUIRE(header_payload_type(fx.captured.back()) == v2gtp::PayloadType::Part20Main);
                {
                    auto variant = decode_frame(fx.captured.back());
                    REQUIRE(variant.get_if<message_20::SessionSetupRequest>() != nullptr);
                }
                REQUIRE(fx.captured.size() == 2);

                // The tail completes the buffered frame; SessionSetup now has a free request
                // slot (its request was transmitted above), so the response advances the FSM.
                fx.session.on_bytes_received(std::vector<uint8_t>(setup_frame.begin() + head, setup_frame.end()));

                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.captured.size() >= 3; }, 1s));

                auto variant = decode_frame(fx.captured.back());
                REQUIRE(variant.get_if<message_20::AuthorizationSetupRequest>() != nullptr);

                REQUIRE_FALSE(fx.session.is_finished());
                REQUIRE_FALSE(fx.timed_out);
            }
        }

        WHEN("frames with a malformed V2GTP header arrive before the SAP response") {
            // [V2G2-800] [V2G20-800]: ignored, and the exchange goes on under the same deadline.
            fx.session.start();
            REQUIRE(run_reactor_until(
                fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));

            fx.session.on_bytes_received(bad_version_header());
            fx.session.on_bytes_received(oversized_header());

            THEN("The session keeps waiting and the valid SAP response still advances it") {
                REQUIRE_FALSE(fx.session.is_finished());
                REQUIRE(fx.captured.size() == 1);

                fx.session.on_bytes_received(frame_payload(v2gtp::PayloadType::SAP, serialize_msg(ok_sap_response())));
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.captured.size() >= 2; }, 1s));
                REQUIRE(decode_frame(fx.captured.back()).get_if<message_20::SessionSetupRequest>() != nullptr);
                REQUIRE_FALSE(fx.session.is_finished());
                REQUIRE_FALSE(fx.timed_out);
            }
        }

        WHEN("a frame with a valid header but a garbage EXI payload arrives") {
            // [V2G2-440] [V2G20-440]: an undecodable response is not a valid response, so it is ignored.
            fx.session.start();
            REQUIRE(run_reactor_until(
                fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));

            fx.session.on_bytes_received(frame_payload(v2gtp::PayloadType::SAP, garbage_exi()));

            THEN("The session keeps waiting and the valid SAP response still advances it") {
                REQUIRE_FALSE(fx.session.is_finished());
                REQUIRE(fx.captured.size() == 1);

                fx.session.on_bytes_received(frame_payload(v2gtp::PayloadType::SAP, serialize_msg(ok_sap_response())));
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.captured.size() >= 2; }, 1s));
                REQUIRE(decode_frame(fx.captured.back()).get_if<message_20::SessionSetupRequest>() != nullptr);
                REQUIRE_FALSE(fx.session.is_finished());
                REQUIRE_FALSE(fx.timed_out);
            }
        }

        WHEN("a valid response of the wrong type arrives while SessionSetupRes is outstanding") {
            // [V2G20-440]: a response to another request is not a valid response, so it is ignored.
            fx.session.start();
            REQUIRE(run_reactor_until(
                fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));
            fx.session.on_bytes_received(frame_payload(v2gtp::PayloadType::SAP, serialize_msg(ok_sap_response())));
            REQUIRE(run_reactor_until(
                fx.reactor, [&]() { return fx.captured.size() >= 2; }, 1s));

            fx.session.on_bytes_received(frame_payload(v2gtp::PayloadType::Part20Main, serialize_msg(stop_response())));

            THEN("The session keeps waiting and the SessionSetupRes still advances it") {
                REQUIRE_FALSE(fx.session.is_finished());
                REQUIRE(fx.captured.size() == 2);

                fx.session.on_bytes_received(
                    frame_payload(v2gtp::PayloadType::Part20Main, serialize_msg(established_session_setup_response())));
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.captured.size() >= 3; }, 1s));
                REQUIRE(decode_frame(fx.captured.back()).get_if<message_20::AuthorizationSetupRequest>() != nullptr);
                REQUIRE_FALSE(fx.session.is_finished());
                REQUIRE_FALSE(fx.timed_out);
            }
        }
    }

    GIVEN("A Session whose evcc_id overflows the SessionSetupRequest encode buffer") {
        // An over-long evccid (> the cb EVCCID buffer) makes the SessionSetupRequest
        // serializer throw; MessageExchange::take_request catches it and returns an
        // empty payload. The Session's encode-failure path must stop the session loudly
        // rather than transmit a truncated frame or hang.
        // 300 chars exceeds the 255-char EVCCID buffer -> CPP2CB_STRING throws.
        SessionFixture fx{std::string(300, 'A')};

        WHEN("the SAP exchange succeeds and the SessionSetupRequest fails to encode") {
            fx.session.start();
            // The SAP request encodes fine (no evccid); transmit it.
            REQUIRE(run_reactor_until(
                fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));

            fx.session.on_bytes_received(frame_payload(v2gtp::PayloadType::SAP, serialize_msg(ok_sap_response())));

            THEN("Nothing further is transmitted, the session stops and finishes, no watchdog fires") {
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.session.is_finished(); }, 1s));
                REQUIRE(fx.session.is_finished());
                // Only the SAP request was ever sent; the un-encodable SessionSetupRequest
                // was dropped, not transmitted truncated.
                REQUIRE(fx.captured.size() == 1);
                // Stopped via the encode-failure path, not the response watchdog.
                REQUIRE_FALSE(fx.timed_out);
            }
        }
    }

    GIVEN("A Session that receives control events around its lifecycle") {
        // deliver_control_event must be safe to call before start() (no FSM yet) and
        // around it; the only reachable state (SupportedAppProtocol) reacts to
        // V2GTP_MESSAGE only, so a CONTROL_MESSAGE emits nothing through the real FSM.
        SessionFixture fx;

        WHEN("a control event is delivered before start()") {
            // No FSM exists yet; this must be a no-op, not a crash.
            fx.session.deliver_control_event(ev::d20::StopCharging{true});

            THEN("Nothing is transmitted and the session is not finished") {
                REQUIRE(fx.captured.empty());
                REQUIRE_FALSE(fx.session.is_finished());
            }
        }

        WHEN("a control event is delivered after start()") {
            fx.session.start();
            REQUIRE(run_reactor_until(
                fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));
            const auto count_after_sap = fx.captured.size();

            fx.session.deliver_control_event(ev::d20::StopCharging{true});

            THEN("The SupportedAppProtocol state ignores it: no extra frame is produced") {
                // Give the reactor a chance to flush any (erroneously) produced request.
                run_reactor_until(
                    fx.reactor, [&]() { return fx.captured.size() > count_after_sap; }, 100ms);
                REQUIRE(fx.captured.size() == count_after_sap);
            }
        }
    }

    GIVEN("A Session whose timed_out feedback throws on first invocation") {
        // timed_out() invokes a consumer-supplied std::function, which can throw. The
        // watchdog handler must signal timed_out EXACTLY ONCE: if it fired the callback
        // inside the FSM-feed try AND again in the catch, a throwing callback would be
        // invoked twice and the catch's re-throw would escape into the reactor's
        // unguarded poll loop. Pin one-shot delivery.
        SessionFixture fx;
        fx.timed_out_throws = true;

        WHEN("start() runs and the response watchdog fires") {
            fx.session.start();

            // Drive the SAP request out (which arms the watchdog), then keep polling so
            // the watchdog expires. The throwing callback escapes the reactor's
            // poll_impl, so guard the run and stop once timed_out has been invoked.
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (fx.timed_out_count == 0 and std::chrono::steady_clock::now() < deadline) {
                try {
                    fx.reactor.poll(1ms);
                    fx.reactor.run_actions();
                } catch (const std::exception&) {
                    // The watchdog fired and the consumer callback threw; that escape is
                    // the documented behavior. Stop polling and assert the call count.
                    break;
                }
            }

            THEN("timed_out is invoked exactly once and the session is stopped") {
                REQUIRE(fx.timed_out_count == 1);
                // stop_session() ran before the callback, so the session is finished
                // regardless of the throw.
                REQUIRE(fx.session.is_finished());
            }
        }
    }
}

SCENARIO("ISO15118-20 EV dropped frames leave a queued request's watchdog disarmed") {
    GIVEN("A Session whose send delay outlasts its response watchdog") {
        SessionFixture fx{"EVTESTID01", ev::session::SessionTiming{200ms, 60ms}};

        WHEN("an unsupported frame arrives while the next request is still held") {
            fx.session.start();
            REQUIRE(run_reactor_until(
                fx.reactor, [&]() { return fx.captured.size() == 1; }, 1s));
            fx.session.on_bytes_received(frame_payload(v2gtp::PayloadType::SAP, serialize_msg(ok_sap_response())));
            fx.session.on_bytes_received(
                frame_payload(static_cast<v2gtp::PayloadType>(0x9000), std::vector<uint8_t>{0x00}));

            THEN("the queued request is sent before its response watchdog starts") {
                REQUIRE_FALSE(run_reactor_until(
                    fx.reactor, [&]() { return fx.timed_out; }, 100ms));
                REQUIRE(fx.captured.size() == 1);
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.captured.size() == 2; }, 1s));
                REQUIRE(decode_frame(fx.captured.back()).get_if<message_20::SessionSetupRequest>() != nullptr);
                REQUIRE_FALSE(fx.timed_out);
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.session.is_finished(); }, 1s));
                REQUIRE(fx.timed_out_count == 1);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Session stops loudly when the outbound send is refused") {
    // The outbound seam reports its result. A refused send leaves no frame on the wire,
    // so arming the response watchdog would only masquerade the failure as a timeout.
    // transmit_pending must stop the session loudly instead.
    SessionFixture fx;
    fx.refuse_send = true;

    GIVEN("A Session whose outbound seam refuses the SAP request") {
        fx.session.start();

        WHEN("the send-delay elapses and the first frame is refused") {
            THEN("The session stops loudly the same pass, not via the response watchdog") {
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.session.is_finished(); }, 1s));
                // The send was attempted exactly once and refused.
                REQUIRE(fx.send_attempts == 1);
                // Stopped via the send-failure path, not the watchdog.
                REQUIRE_FALSE(fx.timed_out);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Session that gives up on a refused send leaves no timer to fire later") {
    // Authorization arms its ongoing guard on entry. Giving up on the refused AuthorizationReq must
    // disarm it, or it fires on the finished session and reports a timeout that never happened.
    GIVEN("A Session with a 50 ms Authorization bound walked to the AuthorizationSetupReq") {
        ev::d20::SessionOptions options{};
        options.d20.authorization_timeout = 50ms;
        SessionFixture fx{"EVTESTID01",
                          ev::session::SessionTiming{5ms, 1000ms},
                          ev::params::DcChargeParams{},
                          default_advertised_app_protocols(),
                          message_20::datatypes::ServiceCategory::DC,
                          ev::params::AcChargeParams{},
                          default_der_control_functions(),
                          true,
                          options};
        fx.session.start();
        REQUIRE(run_reactor_until(
            fx.reactor, [&]() { return fx.captured.size() == 1; }, 1s));
        fx.session.on_bytes_received(frame_payload(v2gtp::PayloadType::SAP, serialize_msg(ok_sap_response())));
        REQUIRE(run_reactor_until(
            fx.reactor, [&]() { return fx.captured.size() == 2; }, 1s));
        fx.session.on_bytes_received(
            frame_payload(v2gtp::PayloadType::Part20Main, serialize_msg(established_session_setup_response())));
        REQUIRE(run_reactor_until(
            fx.reactor, [&]() { return fx.captured.size() == 3; }, 1s));

        WHEN("the AuthorizationReq the AuthorizationSetupRes leads to is refused") {
            fx.refuse_send = true;
            message_20::AuthorizationSetupResponse res{};
            res.header.session_id = established_session_setup_response().header.session_id;
            res.response_code = message_20::datatypes::ResponseCode::OK;
            res.authorization_services = {message_20::datatypes::Authorization::EIM};
            res.certificate_installation_service = false;
            res.authorization_mode = message_20::datatypes::EIM_ASResAuthorizationMode{};
            fx.session.on_bytes_received(frame_payload(v2gtp::PayloadType::Part20Main, serialize_msg(res)));

            THEN("the session finishes and no timeout follows past the Authorization bound") {
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.session.is_finished(); }, 1s));
                REQUIRE(fx.send_attempts == 4);
                REQUIRE_FALSE(run_reactor_until(
                    fx.reactor, [&]() { return fx.timed_out; }, 150ms));
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Session ends an exchange that draws only invalid messages as a timeout") {
    // The ignored message neither extends nor re-arms the response deadline: it arrives 300 ms into
    // a 400 ms window, and the watchdog still fires before a re-armed one could.
    GIVEN("A Session with a 400 ms response watchdog waiting for the SAP response") {
        SessionFixture fx{"EVTESTID01", ev::session::SessionTiming{5ms, 400ms}};
        fx.session.start();
        REQUIRE(run_reactor_until(
            fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));
        REQUIRE_FALSE(run_reactor_until(
            fx.reactor, [&]() { return fx.timed_out; }, 300ms));

        const auto times_out_on_original_deadline = [&]() {
            REQUIRE_FALSE(fx.session.is_finished());
            REQUIRE(run_reactor_until(
                fx.reactor, [&]() { return fx.timed_out; }, 250ms));
            REQUIRE(fx.session.is_finished());
            REQUIRE(fx.captured.size() == 1);
        };

        WHEN("a frame with a malformed V2GTP header arrives") {
            fx.session.on_bytes_received(bad_version_header());
            THEN("the session ends through the response watchdog") {
                times_out_on_original_deadline();
            }
        }

        WHEN("a frame with an oversized payload length arrives") {
            fx.session.on_bytes_received(oversized_header());
            THEN("the session ends through the response watchdog") {
                times_out_on_original_deadline();
            }
        }

        WHEN("an undecodable EXI payload arrives") {
            fx.session.on_bytes_received(frame_payload(v2gtp::PayloadType::SAP, garbage_exi()));
            THEN("the session ends through the response watchdog") {
                times_out_on_original_deadline();
            }
        }

        WHEN("a valid response of the wrong type arrives") {
            fx.session.on_bytes_received(
                frame_payload(v2gtp::PayloadType::Part20Main, serialize_msg(established_session_setup_response())));
            THEN("the session ends through the response watchdog") {
                times_out_on_original_deadline();
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Session keeps the send delay of a held request across an ignored message") {
    GIVEN("A Session holding its SessionSetupReq for a 200 ms send delay") {
        SessionFixture fx{"EVTESTID01", ev::session::SessionTiming{200ms, 1000ms}};
        fx.session.start();
        REQUIRE(run_reactor_until(
            fx.reactor, [&]() { return fx.captured.size() == 1; }, 1s));
        fx.session.on_bytes_received(frame_payload(v2gtp::PayloadType::SAP, serialize_msg(ok_sap_response())));
        REQUIRE_FALSE(run_reactor_until(
            fx.reactor, [&]() { return fx.captured.size() == 2; }, 150ms));

        WHEN("an undecodable EXI payload arrives before the delay elapses") {
            fx.session.on_bytes_received(frame_payload(v2gtp::PayloadType::SAP, garbage_exi()));

            THEN("the request goes out on its original schedule") {
                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.captured.size() == 2; }, 120ms));
                REQUIRE(decode_frame(fx.captured.back()).get_if<message_20::SessionSetupRequest>() != nullptr);
                REQUIRE_FALSE(fx.session.is_finished());
            }
        }
    }
}
