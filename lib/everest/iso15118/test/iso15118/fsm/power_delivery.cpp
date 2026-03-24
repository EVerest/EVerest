// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/ac_charge_loop.hpp>
#include <iso15118/d20/state/dc_charge_loop.hpp>
#include <iso15118/d20/state/power_delivery.hpp>
#include <iso15118/d20/state/session_stop.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

SCENARIO("ISO15118-20 power delivery state transitions") {

    const d20::EvseSetupConfig evse_setup = create_default_evse_setup();

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();

    GIVEN("Bad case - Unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::PowerDelivery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::PowerDeliveryRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;
        req.processing = dt::Processing::Ongoing;
        req.charge_progress = dt::Progress::Start;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::PowerDelivery);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::PowerDeliveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(res.status.has_value() == false);
        }
    }

    GIVEN("Not so bad case - WARNING_StandbyNotAllowed") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::PowerDelivery>()};

        ctx.session = d20::Session();

        message_20::PowerDeliveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.processing = dt::Processing::Ongoing;
        req.charge_progress = dt::Progress::Standby;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::PowerDelivery);

            const auto response_message = ctx.get_response<message_20::PowerDeliveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::WARNING_StandbyNotAllowed);
            REQUIRE(res.status.has_value() == false);
        }
    }

    GIVEN("Bad case - AC ContactorError") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::PowerDelivery>()};

        ctx.session = d20::Session();

        // Set control event for contactor error (closed = false means error)
        state_helper.set_active_control_event(d20::ClosedContactor{false});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::PowerDeliveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.processing = dt::Processing::Ongoing;
        req.charge_progress = dt::Progress::Start;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::PowerDelivery);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::PowerDeliveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ContactorError);
            REQUIRE(res.status.has_value() == false);
        }
    }
}
