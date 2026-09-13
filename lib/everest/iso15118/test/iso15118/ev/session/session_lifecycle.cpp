// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Session teardown paths: terminate(), on_peer_closed() and the EV-initiated
// stop/pause walk into SessionStop.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/supported_app_protocol.hpp>

#include <iso15118/ev/d20/control_event.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using namespace iso15118::ev::test;

namespace {

using PT = io::v2gtp::PayloadType;
using message_20::datatypes::ChargingSession;
using message_20::datatypes::ResponseCode;

constexpr message_20::datatypes::SessionId LIFECYCLE_SESSION_ID{0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11};

message_20::SupportedAppProtocolResponse ok_sap_response() {
    return {message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1};
}

message_20::SessionSetupResponse established_session_setup_response() {
    message_20::SessionSetupResponse res{};
    res.response_code = ResponseCode::OK_NewSessionEstablished;
    res.header.session_id = LIFECYCLE_SESSION_ID;
    res.evseid = "DE*PNX*E12345";
    return res;
}

message_20::AuthorizationSetupResponse ok_authorization_setup_response() {
    auto res = ok_res<message_20::AuthorizationSetupResponse>(LIFECYCLE_SESSION_ID);
    res.authorization_services = {message_20::datatypes::Authorization::EIM};
    res.certificate_installation_service = false;
    res.authorization_mode = message_20::datatypes::EIM_ASResAuthorizationMode{};
    return res;
}

// start() -> SAP -> SessionSetup -> AuthorizationSetupRequest on the wire.
void walk_to_authorization_setup(SessionFixture& fx) {
    fx.session.start();
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));
    inject_then_expect<message_20::SessionSetupRequest>(fx, "SAP -> SessionSetup", ok_sap_response(), PT::SAP);
    inject_then_expect<message_20::AuthorizationSetupRequest>(fx, "SessionSetup -> AuthorizationSetup",
                                                              established_session_setup_response(), PT::Part20Main);
}

} // namespace

SCENARIO("ISO15118-20 EV Session terminate() tears the session down without SessionStop") {
    GIVEN("a Session awaiting the SupportedAppProtocol response") {
        SessionFixture fx;
        int finished_count = 0;
        fx.session.set_on_finished([&finished_count]() { ++finished_count; });

        fx.session.start();
        REQUIRE(run_reactor_until(
            fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));

        WHEN("terminate() is called") {
            fx.session.terminate();

            THEN("the session is finished, DLINK_TERMINATE is signalled once and on_finished fired once") {
                REQUIRE(fx.session.is_finished());
                REQUIRE(fx.signals == std::vector<ev::feedback::Signal>{ev::feedback::Signal::DLINK_TERMINATE});
                REQUIRE(finished_count == 1);
                REQUIRE_FALSE(fx.session.is_paused());
            }

            THEN("no further frame is transmitted and no watchdog fires") {
                run_reactor_until(
                    fx.reactor, [&]() { return fx.captured.size() > 1; }, 200ms);
                REQUIRE(fx.captured.size() == 1);
                REQUIRE_FALSE(fx.timed_out);
            }

            THEN("a second terminate() signals nothing further") {
                fx.session.terminate();
                REQUIRE(fx.signals.size() == 1);
                REQUIRE(finished_count == 1);
            }
        }
    }

    GIVEN("a Session holding an untransmitted request") {
        SessionFixture fx;
        fx.session.start();

        WHEN("terminate() runs before the send delay elapses") {
            fx.session.terminate();

            THEN("the held request is discarded and never reaches the wire") {
                REQUIRE(fx.session.is_finished());
                run_reactor_until(
                    fx.reactor, [&]() { return not fx.captured.empty(); }, 200ms);
                REQUIRE(fx.captured.empty());
                REQUIRE(fx.send_attempts == 0);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Session on_peer_closed ends the session") {
    GIVEN("a Session that has sent its SessionSetupRequest") {
        SessionFixture fx;
        int finished_count = 0;
        fx.session.set_on_finished([&finished_count]() { ++finished_count; });

        fx.session.start();
        REQUIRE(run_reactor_until(
            fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));
        inject_then_expect<message_20::SessionSetupRequest>(fx, "SAP -> SessionSetup", ok_sap_response(), PT::SAP);

        WHEN("the peer closes the connection mid-session") {
            fx.session.on_peer_closed();

            THEN("the session finishes and DLINK_TERMINATE is signalled once") {
                REQUIRE(fx.session.is_finished());
                REQUIRE(fx.signals == std::vector<ev::feedback::Signal>{ev::feedback::Signal::DLINK_TERMINATE});
                REQUIRE(finished_count == 1);
            }

            THEN("a second close signals nothing further and nothing is transmitted") {
                fx.session.on_peer_closed();
                run_reactor_until(
                    fx.reactor, [&]() { return fx.captured.size() > 2; }, 200ms);
                REQUIRE(fx.signals.size() == 1);
                REQUIRE(fx.captured.size() == 2);
                REQUIRE_FALSE(fx.timed_out);
            }
        }
    }

    GIVEN("a Session that completed a graceful SessionStop") {
        SessionFixture fx;
        int finished_count = 0;
        fx.session.set_on_finished([&finished_count]() { ++finished_count; });

        walk_to_authorization_setup(fx);
        fx.session.deliver_control_event(ev::d20::StopCharging{true});
        inject_then_expect<message_20::SessionStopRequest>(fx, "EV stop -> SessionStop",
                                                           ok_authorization_setup_response(), PT::Part20Main);

        fx.session.on_bytes_received(frame_payload(
            PT::Part20Main, serialize_msg(ok_res<message_20::SessionStopResponse>(LIFECYCLE_SESSION_ID))));
        REQUIRE(fx.session.is_finished());
        REQUIRE(fx.signals.size() == 1);

        WHEN("the peer then closes the connection") {
            fx.session.on_peer_closed();

            THEN("no second signal and no second on_finished are emitted") {
                REQUIRE(fx.signals == std::vector<ev::feedback::Signal>{ev::feedback::Signal::DLINK_TERMINATE});
                REQUIRE(finished_count == 1);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Session stop request ends the session with SessionStop(Terminate)") {
    GIVEN("a Session walked to AuthorizationSetup with StopCharging requested") {
        SessionFixture fx;
        walk_to_authorization_setup(fx);
        fx.session.deliver_control_event(ev::d20::StopCharging{true});

        WHEN("the AuthorizationSetupResponse arrives") {
            const auto req = inject_then_expect<message_20::SessionStopRequest>(
                fx, "EV stop -> SessionStop", ok_authorization_setup_response(), PT::Part20Main);

            THEN("the SessionStopRequest carries Terminate [V2G20-2644]") {
                REQUIRE(req.charging_session == ChargingSession::Terminate);
            }

            THEN("the SessionStopResponse finishes the session without pausing it") {
                fx.session.on_bytes_received(frame_payload(
                    PT::Part20Main, serialize_msg(ok_res<message_20::SessionStopResponse>(LIFECYCLE_SESSION_ID))));
                REQUIRE(fx.session.is_finished());
                REQUIRE_FALSE(fx.session.is_paused());
                REQUIRE(fx.signals == std::vector<ev::feedback::Signal>{ev::feedback::Signal::DLINK_TERMINATE});
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Session pause request ends the session with SessionStop(Pause)") {
    GIVEN("a Session with PauseCharging delivered before start()") {
        SessionFixture fx;
        fx.session.deliver_control_event(ev::d20::PauseCharging{true});
        REQUIRE(fx.captured.empty());

        walk_to_authorization_setup(fx);

        WHEN("the AuthorizationSetupResponse arrives") {
            const auto req = inject_then_expect<message_20::SessionStopRequest>(
                fx, "EV pause -> SessionStop", ok_authorization_setup_response(), PT::Part20Main);

            THEN("the SessionStopRequest carries Pause") {
                REQUIRE(req.charging_session == ChargingSession::Pause);
            }

            THEN("the SessionStopResponse leaves the session paused and signals DLINK_PAUSE") {
                fx.session.on_bytes_received(frame_payload(
                    PT::Part20Main, serialize_msg(ok_res<message_20::SessionStopResponse>(LIFECYCLE_SESSION_ID))));
                REQUIRE(fx.session.is_finished());
                REQUIRE(fx.session.is_paused());
                REQUIRE(fx.session.session_id() == LIFECYCLE_SESSION_ID);
                REQUIRE(fx.signals == std::vector<ev::feedback::Signal>{ev::feedback::Signal::DLINK_PAUSE});
            }
        }
    }
}
