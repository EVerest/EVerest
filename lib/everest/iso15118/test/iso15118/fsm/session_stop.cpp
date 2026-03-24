// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/session_stop.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

SCENARIO("ISO15118-20 session stop state transitions") {

    const d20::EvseSetupConfig evse_setup = create_default_evse_setup();

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();

    GIVEN("Bad case - Unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::SessionStop>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::SessionStopRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;
        req.charging_session = dt::ChargingSession::Terminate;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::SessionStop);

            const auto response_message = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
        }
    }

    GIVEN("Good Case") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::SessionStop>()};

        ctx.session = d20::Session();

        message_20::SessionStopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.charging_session = dt::ChargingSession::Terminate;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::SessionStop);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("Bad case - FAILED_NoServiceRenegotiationSupported") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::SessionStop>()};

        ctx.session = d20::Session();
        ctx.session.service_renegotiation_supported = false;

        message_20::SessionStopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.charging_session = dt::ChargingSession::ServiceRenegotiation;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::SessionStop);

            const auto response_message = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_NoServiceRenegotiationSupported);
        }
    }

    // GIVEN("Bad case - Dynamic mode, FAILED_PauseNotAllowed") {} // TODO(sl): EVCC requests Pause, but Secc didnt
    // initiated

    // GIVEN("Bad case - Scheduled mode , FAILED_PauseNotAllowed") {} // TODO(sl): Check current EVPowerProfileEntry for
    // 0kW

    // GIVEN("Bad case - State B was not measuered -> FAILED") {} // TODO(sl): check evse_manager?
}
