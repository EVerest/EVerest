// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/dc_welding_detection.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

namespace {

message_20::SessionStopRequest pause_request(const d20::Session& session) {
    message_20::SessionStopRequest req;
    req.header.session_id = session.get_id();
    req.header.timestamp = 1691411798;
    req.charging_session = dt::ChargingSession::Pause;
    return req;
}

} // namespace

SCENARIO("A SessionStopReq(Pause) outside the SessionStop state") {

    std::optional<d20::PauseContext> pause_ctx{d20::PauseContext{}};
    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(session::SessionConfig(create_default_evse_setup()), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();

    GIVEN("A dynamic-mode EV pausing in DC_WeldingDetection without the SECC having asked for it") {
        ctx.session = d20::Session(
            d20::SelectedServiceParameters(dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Dynamic,
                                           dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing));

        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_WeldingDetection>()};

        state_helper.handle_request(pause_request(ctx.session));
        REQUIRE_FALSE(fsm.feed(d20::Event::V2GTP_MESSAGE).transitioned());

        THEN("the FAILED_PauseNotAllowed on the wire ends the session rather than pausing the link") {
            const auto res = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res->response_code == dt::ResponseCode::FAILED_PauseNotAllowed);

            REQUIRE(ctx.session_stopped);
            REQUIRE_FALSE(ctx.session_paused);
            REQUIRE(ctx.session_stop_res_pending == session::feedback::SessionStopAction::FailedTermination);
            REQUIRE_FALSE(pause_ctx.has_value());
        }
    }
}
