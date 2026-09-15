// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

#include "helper.hpp"

#include <iso15118/d20/state/power_delivery.hpp>
#include <iso15118/message/power_delivery.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

// AC_CLOSE_CONTACTOR_TIMEOUT in src/iso15118/d20/state/power_delivery.cpp, plus a margin.
constexpr auto WAIT_OUT_CONTACTOR_TIMEOUT = std::chrono::milliseconds(1500);

SCENARIO("ISO15118-20 power delivery contactor handling") {

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};

    std::vector<session::feedback::Signal> signals;
    session::feedback::Callbacks callbacks{};
    callbacks.signal = [&signals](session::feedback::Signal signal) { signals.push_back(signal); };

    auto state_helper = FsmStateHelper(d20::SessionConfig(create_default_evse_setup()), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();
    ctx.session = d20::Session(
        d20::SelectedServiceParameters(dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
                                       dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230));

    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::PowerDelivery>()};

    message_20::PowerDeliveryRequest req;
    req.header.session_id = ctx.session.get_id();
    req.header.timestamp = 1691411798;
    req.processing = dt::Processing::Ongoing;
    req.charge_progress = dt::Progress::Start;

    // PowerDeliveryReq(Start) before the contactor is closed: the request is saved, the contactor is
    // asked to close and the contactor timeout is armed.
    state_helper.handle_request(req);
    REQUIRE(fsm.feed(d20::Event::V2GTP_MESSAGE).transitioned() == false);
    REQUIRE(std::count(signals.begin(), signals.end(), session::feedback::Signal::AC_CLOSE_CONTACTOR) == 1);

    GIVEN("The contactor closes") {
        state_helper.set_active_control_event(d20::ClosedContactor(true));
        const auto result = fsm.feed(d20::Event::CONTROL_MESSAGE);

        THEN("The saved request is answered and the charge loop is entered") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::PowerDeliveryResponse>();
            REQUIRE(response_message.has_value());
            REQUIRE(response_message.value().response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("The session tears down before the contactor closes") {
        ctx.request_shutdown();

        // EvseManager publishes ClosedContactor{false} on CPEvent::PowerOff during teardown.
        state_helper.set_active_control_event(d20::ClosedContactor(false));
        REQUIRE(fsm.feed(d20::Event::CONTROL_MESSAGE).transitioned() == false);

        // Drive the timeouts the way Session::loop does: only a timeout that Timeouts still holds
        // reaches the state machine.
        std::this_thread::sleep_for(WAIT_OUT_CONTACTOR_TIMEOUT);
        for (const auto timeout : state_helper.get_timeouts().check().value_or(std::vector<d20::TimeoutType>{})) {
            ctx.set_active_timeout(timeout);
            fsm.feed(d20::Event::TIMEOUT);
        }

        THEN("The contactor timeout does not fail the already stopping session") {
            REQUIRE(ctx.session_stopped == false);
            REQUIRE(ctx.get_response<message_20::PowerDeliveryResponse>().has_value() == false);
        }
    }
}
