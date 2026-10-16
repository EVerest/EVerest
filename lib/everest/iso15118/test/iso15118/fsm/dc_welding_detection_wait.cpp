// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// ISO 15118-20 DC_WeldingDetectionRes waits for present values received after its request, so that the
// EVSEPresentVoltage is measured after the request [IEC 61851-23:2023 CC.6.3, CC.7.6.17].
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/dc_welding_detection.hpp>
#include <iso15118/message/dc_welding_detection.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

namespace {

message_20::DC_WeldingDetectionRequest make_welding_detection(const d20::Session& session, dt::Processing processing) {
    message_20::DC_WeldingDetectionRequest req;
    req.header.session_id = session.get_id();
    req.header.timestamp = 1691411798;
    req.processing = processing;
    return req;
}

float response_voltage(const message_20::DC_WeldingDetectionResponse& res) {
    return dt::from_RationalNumber(res.present_voltage);
}

message_20::SessionStopRequest make_session_stop(const d20::Session& session) {
    message_20::SessionStopRequest req;
    req.header.session_id = session.get_id();
    req.header.timestamp = 1691411799;
    req.charging_session = dt::ChargingSession::Terminate;
    return req;
}

} // namespace

SCENARIO("ISO15118-20 DC_WeldingDetectionRes waits for present values received after the request") {
    const auto evse_setup = create_default_evse_setup();
    std::optional<d20::PauseContext> pause_ctx;

    auto state_helper = FsmStateHelper(session::SessionConfig(evse_setup), pause_ctx, {});
    auto& ctx = state_helper.get_context();
    ctx.session = d20::Session(
        d20::SelectedServiceParameters(dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
                                       dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing));

    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_WeldingDetection>()};

    const auto control = [&](const d20::ControlEvent& event) {
        state_helper.set_active_control_event(event);
        return fsm.feed(d20::Event::CONTROL_MESSAGE);
    };
    const auto has_response = [&]() { return state_helper.get_message_exchange().has_response(); };

    // Values from before the request, e.g. sent before the PowerDeliveryRes.
    control(d20::PresentVoltageCurrent{400.0f, 0.0f});

    GIVEN("The EV sends DC_WeldingDetectionReq(Ongoing)") {
        state_helper.handle_request(make_welding_detection(ctx.session, dt::Processing::Ongoing));
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("No response is sent yet") {
            REQUIRE_FALSE(has_response());
        }

        // The first update after the request can carry a sample taken just before it (a polled meter
        // delivers a sample up to one poll interval later); the second one was taken after the request.
        WHEN("One present values update arrives") {
            control(d20::PresentVoltageCurrent{32.0f, 0.0f});

            THEN("The response still waits for a second one") {
                REQUIRE_FALSE(has_response());
            }

            AND_WHEN("No second one arrives in time") {
                ctx.set_active_timeout(d20::TimeoutType::PRESENT_VALUES);
                fsm.feed(d20::Event::TIMEOUT);

                THEN("It is answered with the one that arrived") {
                    const auto res = ctx.get_response<message_20::DC_WeldingDetectionResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::OK);
                    REQUIRE(response_voltage(*res) == 32.0f);
                }
            }
        }

        WHEN("Present values arrive twice") {
            control(d20::PresentVoltageCurrent{32.0f, 0.0f});
            control(d20::PresentVoltageCurrent{8.0f, 0.0f});

            THEN("The response reports the second ones and the state stays") {
                const auto res = ctx.get_response<message_20::DC_WeldingDetectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(response_voltage(*res) == 8.0f);
                REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_WeldingDetection);
            }
        }

        WHEN("No present values arrive in time") {
            ctx.set_active_timeout(d20::TimeoutType::PRESENT_VALUES);
            fsm.feed(d20::Event::TIMEOUT);

            THEN("The response goes out OK with the last values") {
                const auto res = ctx.get_response<message_20::DC_WeldingDetectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
                REQUIRE(response_voltage(*res) == 400.0f);
            }
        }

        WHEN("The EV gives up and sends SessionStopReq(Terminate) instead") {
            state_helper.handle_request(make_session_stop(ctx.session));
            fsm.feed(d20::Event::V2GTP_MESSAGE);

            THEN("The stop is answered and the parked request is dropped") {
                REQUIRE(ctx.get_response<message_20::SessionStopResponse>().has_value());
                REQUIRE(ctx.session_stopped);
                control(d20::PresentVoltageCurrent{0.0f, 0.0f});
                ctx.set_active_timeout(d20::TimeoutType::PRESENT_VALUES);
                fsm.feed(d20::Event::TIMEOUT);
                REQUIRE_FALSE(ctx.get_response<message_20::DC_WeldingDetectionResponse>().has_value());
            }
        }

        WHEN("The session ends while it waits (CP State A)") {
            // The engine sets the flag on CP State A; the harness does likewise.
            ctx.session_stopped = true;

            THEN("Neither present values nor the timeout produce a response") {
                control(d20::PresentVoltageCurrent{0.0f, 0.0f});
                REQUIRE_FALSE(has_response());
                ctx.set_active_timeout(d20::TimeoutType::PRESENT_VALUES);
                fsm.feed(d20::Event::TIMEOUT);
                REQUIRE_FALSE(has_response());
            }
        }
    }

    GIVEN("The EV sends DC_WeldingDetectionReq(Finished)") {
        state_helper.handle_request(make_welding_detection(ctx.session, dt::Processing::Finished));
        fsm.feed(d20::Event::V2GTP_MESSAGE);
        REQUIRE_FALSE(has_response());

        WHEN("Present values arrive twice") {
            REQUIRE_FALSE(control(d20::PresentVoltageCurrent{32.0f, 0.0f}).transitioned());
            const auto result = control(d20::PresentVoltageCurrent{2.0f, 0.0f});

            THEN("The response is sent and SessionStop follows") {
                REQUIRE(result.transitioned());
                REQUIRE(fsm.get_current_state_id() == d20::StateID::SessionStop);
                const auto res = ctx.get_response<message_20::DC_WeldingDetectionResponse>();
                REQUIRE(res.has_value());
                REQUIRE(response_voltage(*res) == 2.0f);
            }
        }
    }

    GIVEN("A DC_WeldingDetectionReq with an unknown session") {
        auto req = make_welding_detection(ctx.session, dt::Processing::Ongoing);
        req.header.session_id = d20::Session().get_id();
        state_helper.handle_request(req);
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("It is rejected at once") {
            const auto res = ctx.get_response<message_20::DC_WeldingDetectionResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res->response_code == dt::ResponseCode::FAILED_UnknownSession);
        }
    }
}
