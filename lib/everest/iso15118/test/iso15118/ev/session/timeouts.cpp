// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Per-state response watchdog and Ongoing guard: the table itself, plus one reactor
// run proving a zero-configured response_timeout falls back to it.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>

#include <iso15118/ev/d20/timeouts.hpp>
#include <iso15118/message/dc_welding_detection.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using namespace iso15118::ev::test;
using iso15118::ev::d20::StateID;

namespace timeouts = iso15118::ev::d20::timeouts;

SCENARIO("ISO15118-20 EV per-message response timeout table") {
    GIVEN("the response timeout table") {
        THEN("ServiceDetail gets the longer V2G_EVCC_Msg_Timeout") {
            REQUIRE(timeouts::response_timeout(StateID::ServiceDetail) == 5000ms);
        }

        THEN("every charge loop gets the short loop timeout") {
            REQUIRE(timeouts::response_timeout(StateID::AC_ChargeLoop) == 500ms);
            REQUIRE(timeouts::response_timeout(StateID::AC_DER_IEC_ChargeLoop) == 500ms);
            REQUIRE(timeouts::response_timeout(StateID::AC_DER_SAE_ChargeLoop) == 500ms);
            REQUIRE(timeouts::response_timeout(StateID::DC_ChargeLoop) == 500ms);
        }

        THEN("every other state gets the default message timeout") {
            REQUIRE(timeouts::response_timeout(StateID::SupportedAppProtocol) == 2000ms);
            REQUIRE(timeouts::response_timeout(StateID::SessionSetup) == 2000ms);
            REQUIRE(timeouts::response_timeout(StateID::DC_CableCheck) == 2000ms);
            REQUIRE(timeouts::response_timeout(StateID::AC_DER_SAE_ChargeParameterDiscovery) == 2000ms);
            REQUIRE(timeouts::response_timeout(StateID::PowerDelivery) == 2000ms);
            REQUIRE(timeouts::response_timeout(StateID::SessionStop) == 2000ms);
        }
    }
}

SCENARIO("ISO15118-20 EV Ongoing guard table") {
    GIVEN("the ongoing timeout table") {
        THEN("the polling states carry their standard bounds") {
            // V2G_EVCC_Ongoing_Timeout for the generic ones, the DC-specific overrides for
            // cable check and pre-charge.
            REQUIRE(timeouts::ongoing_timeout(StateID::Authorization) == 60000ms);
            REQUIRE(timeouts::ongoing_timeout(StateID::ScheduleExchange) == 60000ms);
            REQUIRE(timeouts::ongoing_timeout(StateID::AC_DER_SAE_ChargeParameterDiscovery) == 60000ms);
            REQUIRE(timeouts::ongoing_timeout(StateID::DC_CableCheck) == 40000ms);
            REQUIRE(timeouts::ongoing_timeout(StateID::DC_PreCharge) == 10000ms);
        }

        THEN("states with standard Ongoing bounds are covered") {
            // The defect this guards against is a state that re-sends on Ongoing with no entry
            // here, which holds the EV in that state for as long as the SECC keeps answering.
            for (const auto state :
                 {StateID::Authorization, StateID::ScheduleExchange, StateID::AC_DER_SAE_ChargeParameterDiscovery,
                  StateID::DC_CableCheck, StateID::DC_PreCharge}) {
                REQUIRE(timeouts::ongoing_timeout(state).has_value());
            }
        }

        THEN("a configured authorization timeout replaces only that state's bound") {
            REQUIRE(timeouts::ongoing_timeout(StateID::Authorization, 180000ms) == 180000ms);
            REQUIRE(timeouts::ongoing_timeout(StateID::Authorization, 0ms) == 60000ms);
            REQUIRE(timeouts::ongoing_timeout(StateID::DC_CableCheck, 180000ms) == 40000ms);
            REQUIRE_FALSE(timeouts::ongoing_timeout(StateID::SessionSetup, 180000ms).has_value());
        }

        THEN("states without an Ongoing loop have no bound") {
            REQUIRE_FALSE(timeouts::ongoing_timeout(StateID::SessionSetup).has_value());
            REQUIRE_FALSE(timeouts::ongoing_timeout(StateID::DC_ChargeLoop).has_value());
            REQUIRE_FALSE(timeouts::ongoing_timeout(StateID::SessionStop).has_value());
        }
    }
}

SCENARIO("ISO15118-20 EV pre-session timing constants") {
    THEN("the SDP discovery bounds match the standard") {
        REQUIRE(timeouts::SDP_MAX_REQUESTS == 50);
        REQUIRE(timeouts::SDP_RESEND_INTERVAL == 250ms);
    }
}

SCENARIO("ISO15118-20 EV Session falls back to the table when response_timeout is zero") {
    GIVEN("a Session configured with response_timeout 0") {
        SessionFixture fx{"EVTESTID01", ev::session::SessionTiming{5ms, 0ms}};

        WHEN("the SAP request is sent and no response ever arrives") {
            fx.session.start();
            REQUIRE(run_reactor_until(
                fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));

            THEN("nothing fires early, then the SupportedAppProtocol MESSAGE timeout expires") {
                REQUIRE_FALSE(run_reactor_until(
                    fx.reactor, [&]() { return fx.session.is_finished(); }, 200ms));
                REQUIRE_FALSE(fx.timed_out);

                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.session.is_finished(); }, 2500ms));
                REQUIRE(fx.timed_out_count == 1);
            }
        }
    }
}

SCENARIO("EV dropped frames leave the SessionSetup response deadline unchanged") {
    SessionFixture fx{"EVTESTID01", ev::session::SessionTiming{5ms, 0ms}};
    fx.session.start();
    REQUIRE(run_reactor_until(
        fx.reactor, [&]() { return fx.captured.size() == 1; }, 1s));
    inject_then_expect<message_20::SessionSetupRequest>(
        fx, "SAP -> SessionSetup",
        message_20::SupportedAppProtocolResponse{
            message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation, 1},
        v2gtp::PayloadType::SAP);

    const auto sent_at = std::chrono::steady_clock::now();
    auto next_frame = sent_at + 1500ms;
    int dropped_frames = 0;
    const auto unsupported = frame_payload(static_cast<v2gtp::PayloadType>(0xffff), {0});
    REQUIRE(run_reactor_until(
        fx.reactor,
        [&]() {
            if (std::chrono::steady_clock::now() >= next_frame) {
                fx.session.on_bytes_received(unsupported);
                ++dropped_frames;
                next_frame += 1500ms;
            }
            return fx.session.is_finished();
        },
        2500ms));
    REQUIRE(std::chrono::steady_clock::now() - sent_at >= 1900ms);
    REQUIRE(dropped_frames == 1);
    REQUIRE(fx.timed_out_count == 1);
    REQUIRE(fx.captured.size() == 2);
}

SCENARIO("EV silent welding charger gets the two-second message deadline") {
    std::unique_ptr<SessionFixture> fx;
    SECTION("ISO15118-20") {
        fx = welding_fixture_d20();
    }
    SECTION("ISO15118-2") {
        fx = welding_fixture_d2();
    }
    const auto sent_at = std::chrono::steady_clock::now();
    const auto sent = fx->captured.size();
    REQUIRE(run_reactor_until(
        fx->reactor, [&] { return fx->session.is_finished(); }, 2500ms));
    REQUIRE(std::chrono::steady_clock::now() - sent_at >= 1900ms);
    REQUIRE(fx->captured.size() == sent);
    REQUIRE(fx->welding_failures.size() == 1);
    REQUIRE(std::isnan(fx->welding_failures.front().first));
    REQUIRE(fx->welding_failures.front().second == 0);
    REQUIRE(fx->timed_out_count == 1);
    REQUIRE_FALSE(fx->session.is_paused());
    REQUIRE(fx->signals.back() == ev::feedback::Signal::DLINK_TERMINATE);
}

SCENARIO("ISO15118-20 EV missing Finished response preserves welding outcome and stops without SessionStop") {
    auto fx = welding_fixture_d20();
    const auto decoded = decode_frame(fx->captured.back());
    const auto* first = decoded.get_if<message_20::DC_WeldingDetectionRequest>();
    REQUIRE(first != nullptr);
    auto response = ok_res<message_20::DC_WeldingDetectionResponse>(first->header.session_id);
    int cycles = 1;
    SECTION("safe voltage") {
        response.present_voltage = message_20::datatypes::from_float(0.0f);
    }
    SECTION("failed check") {
        response.present_voltage = message_20::datatypes::from_float(400.0f);
        cycles = 20;
    }
    for (int cycle = 0; cycle < cycles; ++cycle) {
        const auto request = inject_then_expect<message_20::DC_WeldingDetectionRequest>(
            *fx, "welding response", response, v2gtp::PayloadType::Part20DC);
        REQUIRE(request.processing == (cycle + 1 == cycles ? message_20::datatypes::Processing::Finished
                                                           : message_20::datatypes::Processing::Ongoing));
    }
    const auto sent = fx->captured.size();
    REQUIRE(run_reactor_until(
        fx->reactor, [&] { return fx->session.is_finished(); }, 2500ms));
    REQUIRE(fx->captured.size() == sent);
    if (cycles == 1) {
        REQUIRE(fx->welding_failures.empty());
    } else {
        REQUIRE(fx->welding_failures.size() == 1);
        REQUIRE(fx->welding_failures.front().first == 400.0f);
        REQUIRE(fx->welding_failures.front().second == cycles);
    }
    REQUIRE(fx->timed_out_count == 1);
    REQUIRE_FALSE(fx->session.is_paused());
}
