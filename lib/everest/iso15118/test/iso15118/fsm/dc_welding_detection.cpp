// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "helper.hpp"

#include <iso15118/d20/state/dc_charge_loop.hpp>
#include <iso15118/message/dc_welding_detection.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

using Signal = session::feedback::Signal;

namespace {

message_20::PowerDeliveryRequest make_power_delivery_stop(const d20::Session& session) {
    message_20::PowerDeliveryRequest req;
    req.header.session_id = session.get_id();
    req.header.timestamp = 1691411798;
    req.processing = dt::Processing::Finished;
    req.charge_progress = dt::Progress::Stop;
    return req;
}

message_20::DC_WeldingDetectionRequest make_welding_detection(const d20::Session& session, dt::Processing processing) {
    message_20::DC_WeldingDetectionRequest req;
    req.header.session_id = session.get_id();
    req.header.timestamp = 1691411798;
    req.processing = processing;
    return req;
}

message_20::SessionStopRequest make_session_stop(const d20::Session& session) {
    message_20::SessionStopRequest req;
    req.header.session_id = session.get_id();
    req.header.timestamp = 1691411798;
    req.charging_session = dt::ChargingSession::Terminate;
    return req;
}

// Rounded to 0.1 V: the RationalNumber round trip is not exact in float.
float volts(const dt::RationalNumber& value) {
    return std::round(dt::from_RationalNumber(value) * 10.0f) / 10.0f;
}

long count(const std::vector<Signal>& signals, Signal signal) {
    return std::count(signals.begin(), signals.end(), signal);
}

} // namespace

SCENARIO("ISO15118-20 DC welding detection phase") {
    const auto evse_setup = create_default_evse_setup();
    std::optional<d20::PauseContext> pause_ctx;

    std::vector<Signal> signals;
    session::feedback::Callbacks callbacks{};
    callbacks.signal = [&signals](Signal signal) { signals.push_back(signal); };

    auto state_helper = FsmStateHelper(session::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto& ctx = state_helper.get_context();
    ctx.session = d20::Session(
        d20::SelectedServiceParameters(dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
                                       dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing));

    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

    // The engine latches present values on the context before every state sees them.
    const auto push = [&](const d20::PresentVoltageCurrent& values) {
        ctx.cache_present_values = values;
        ctx.welding_detection.observe(values);
        state_helper.set_active_control_event(values);
        return fsm.feed(d20::Event::CONTROL_MESSAGE);
    };

    push(d20::PresentVoltageCurrent{20.0f, 0.0f});
    state_helper.handle_request(make_power_delivery_stop(ctx.session));
    fsm.feed(d20::Event::V2GTP_MESSAGE);

    GIVEN("PowerDeliveryReq(Stop)") {
        THEN("The phase starts once, after the power path was opened") {
            REQUIRE(count(signals, Signal::WELDING_DETECTION_STARTED) == 1);
            const auto open = std::find(signals.begin(), signals.end(), Signal::DC_OPEN_CONTACTOR);
            const auto started = std::find(signals.begin(), signals.end(), Signal::WELDING_DETECTION_STARTED);
            REQUIRE(open < started);
        }
    }

    GIVEN("DC_WeldingDetection after the power path is reported off") {
        state_helper.set_active_control_event(d20::ClosedContactor{false});
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_WeldingDetection);
        // Drop the PowerDeliveryRes, as the session does once it is sent.
        state_helper.get_message_exchange().check_and_clear_response();

        WHEN("The first DC_WeldingDetectionReq arrives") {
            // A marked value from before the request does not count: the EV may have switched its
            // disconnection device since (Table CC.9 t105 to t106).
            push(d20::PresentVoltageCurrent{900.0f, 0.0f, true});
            state_helper.handle_request(make_welding_detection(ctx.session, dt::Processing::Ongoing));
            fsm.feed(d20::Event::V2GTP_MESSAGE);

            THEN("It is held back") {
                REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
            }

            AND_WHEN("Power supply values arrive") {
                push(d20::PresentVoltageCurrent{21.0f, 0.0f});
                push(d20::PresentVoltageCurrent{21.0f, 0.0f});

                THEN("It is still held back") {
                    REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
                }
            }

            AND_WHEN("One welding detection voltage arrives") {
                push(d20::PresentVoltageCurrent{398.0f, 0.0f, true});

                THEN("It is still held back: the value may predate the request") {
                    REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
                }

                AND_WHEN("The second one arrives") {
                    push(d20::PresentVoltageCurrent{2.0f, 0.0f, true});

                    THEN("It is answered with that voltage") {
                        const auto res = ctx.get_response<message_20::DC_WeldingDetectionResponse>();
                        REQUIRE(res.has_value());
                        REQUIRE(res->response_code == dt::ResponseCode::OK);
                        REQUIRE(volts(res->present_voltage) == 2.0f);
                    }

                    AND_WHEN("The next DC_WeldingDetectionReq arrives") {
                        state_helper.get_message_exchange().check_and_clear_response();
                        state_helper.handle_request(make_welding_detection(ctx.session, dt::Processing::Ongoing));
                        fsm.feed(d20::Event::V2GTP_MESSAGE);

                        THEN("It waits for values measured after it as well") {
                            REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
                            push(d20::PresentVoltageCurrent{1.0f, 0.0f, true});
                            push(d20::PresentVoltageCurrent{1.0f, 0.0f, true});
                            const auto res = ctx.get_response<message_20::DC_WeldingDetectionResponse>();
                            REQUIRE(res.has_value());
                            REQUIRE(volts(res->present_voltage) == 1.0f);
                        }
                    }
                }

                AND_WHEN("No second one arrives in time") {
                    ctx.set_active_timeout(d20::TimeoutType::WELDING_VOLTAGE);
                    fsm.feed(d20::Event::TIMEOUT);

                    THEN("It is answered with the one that arrived") {
                        const auto res = ctx.get_response<message_20::DC_WeldingDetectionResponse>();
                        REQUIRE(res.has_value());
                        REQUIRE(volts(res->present_voltage) == 398.0f);
                    }
                }
            }

            AND_WHEN("No welding detection voltage arrives in time") {
                ctx.set_active_timeout(d20::TimeoutType::WELDING_VOLTAGE);
                fsm.feed(d20::Event::TIMEOUT);

                THEN("It is answered with the last present voltage") {
                    const auto res = ctx.get_response<message_20::DC_WeldingDetectionResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::OK);
                    REQUIRE(volts(res->present_voltage) == 900.0f);
                }
            }

            AND_WHEN("The session ends while it is parked") {
                ctx.session_stopped = true;
                push(d20::PresentVoltageCurrent{1.0f, 0.0f, true});
                push(d20::PresentVoltageCurrent{1.0f, 0.0f, true});

                THEN("It gets no answer") {
                    REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
                }
            }
        }

        WHEN("The EV finishes welding detection") {
            state_helper.handle_request(make_welding_detection(ctx.session, dt::Processing::Finished));
            fsm.feed(d20::Event::V2GTP_MESSAGE);
            push(d20::PresentVoltageCurrent{398.0f, 0.0f, true});
            const auto result = push(d20::PresentVoltageCurrent{398.0f, 0.0f, true});

            THEN("The phase finishes once and SessionStop follows") {
                REQUIRE(result.transitioned());
                REQUIRE(fsm.get_current_state_id() == d20::StateID::SessionStop);
                REQUIRE(count(signals, Signal::WELDING_DETECTION_FINISHED) == 1);
            }
        }

        WHEN("The EV ends the session without welding detection") {
            state_helper.handle_request(make_session_stop(ctx.session));
            fsm.feed(d20::Event::V2GTP_MESSAGE);

            THEN("The phase finishes as well") {
                REQUIRE(ctx.session_stopped);
                REQUIRE(count(signals, Signal::WELDING_DETECTION_FINISHED) == 1);
            }
        }
    }
}
