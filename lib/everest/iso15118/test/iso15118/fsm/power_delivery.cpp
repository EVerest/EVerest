// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

#include "helper.hpp"

#include <iso15118/d20/state/power_delivery.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

// AC_CLOSE_CONTACTOR_TIMEOUT in src/iso15118/d20/state/power_delivery.cpp, plus a margin.
constexpr auto WAIT_OUT_CONTACTOR_TIMEOUT = std::chrono::milliseconds(1500);

SCENARIO("ISO15118-20 power delivery contactor handling") {

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};

    std::vector<session::feedback::Signal> signals;
    session::feedback::Callbacks callbacks{};
    callbacks.signal = [&signals](session::feedback::Signal signal) { signals.push_back(signal); };

    auto state_helper = FsmStateHelper(session::SessionConfig(create_default_evse_setup()), pause_ctx, callbacks);
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

        THEN("The saved request is answered with a terminate notification, not a contactor error") {
            const auto response_message = ctx.get_response<message_20::PowerDeliveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& response = response_message.value();
            REQUIRE(response.response_code == dt::ResponseCode::OK);
            REQUIRE(response.status.has_value());
            REQUIRE(response.status.value().notification == dt::EvseNotification::Terminate);
            REQUIRE(response.status.value().notification_max_delay == 0);
            REQUIRE(ctx.session_stopped == true);

            // Drive the timeouts the way Session::loop does: the contactor timeout is cancelled, so
            // it never fires a second, failing response at the EV.
            std::this_thread::sleep_for(WAIT_OUT_CONTACTOR_TIMEOUT);
            REQUIRE(state_helper.get_timeouts().check().empty());
        }
    }
}

SCENARIO("ISO15118-20 power delivery answers before the contactor closes") {

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};

    std::vector<session::feedback::Signal> signals;
    std::vector<std::string> ev_termination_codes;
    session::feedback::Callbacks callbacks{};
    callbacks.signal = [&signals](session::feedback::Signal signal) { signals.push_back(signal); };
    callbacks.ev_termination = [&ev_termination_codes](const std::string& code, const std::string&) {
        ev_termination_codes.push_back(code);
    };

    auto state_helper = FsmStateHelper(session::SessionConfig(create_default_evse_setup()), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();
    ctx.session = d20::Session(
        d20::SelectedServiceParameters(dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
                                       dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230));

    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::PowerDelivery>()};

    message_20::PowerDeliveryRequest req;
    req.header.session_id = ctx.session.get_id();
    req.header.timestamp = 1691411798;
    req.processing = dt::Processing::Finished;
    req.charge_progress = dt::Progress::Start;

    GIVEN("A PowerDeliveryReq(Start) of an unknown session") {
        req.header.session_id = d20::Session().get_id();
        state_helper.handle_request(req);
        REQUIRE_FALSE(fsm.feed(d20::Event::V2GTP_MESSAGE).transitioned());

        THEN("it is rejected without closing the contactor") {
            const auto res = ctx.get_response<message_20::PowerDeliveryResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res->response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(ctx.session_stopped);
            REQUIRE(std::count(signals.begin(), signals.end(), session::feedback::Signal::AC_CLOSE_CONTACTOR) == 0);
            REQUIRE(state_helper.get_timeouts().check().empty());
        }
    }

    GIVEN("A PowerDeliveryReq(Start) selecting a schedule tuple that was never offered") {
        auto& profile = req.power_profile.emplace();
        profile.time_anchor = 1691411798 * d20::MICROSECONDS_PER_SECOND;
        profile.control_mode.emplace<dt::Scheduled_EVPPTControlMode>().selected_schedule = 7;
        state_helper.handle_request(req);
        REQUIRE_FALSE(fsm.feed(d20::Event::V2GTP_MESSAGE).transitioned());

        THEN("it is rejected without closing the contactor [V2G20-479]") {
            const auto res = ctx.get_response<message_20::PowerDeliveryResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res->response_code == dt::ResponseCode::FAILED_ScheduleSelectionInvalid);
            REQUIRE(ctx.session_stopped);
            REQUIRE(std::count(signals.begin(), signals.end(), session::feedback::Signal::AC_CLOSE_CONTACTOR) == 0);
        }
    }

    GIVEN("A SessionStopReq while the PowerDeliveryReq(Start) still waits for the contactor") {
        state_helper.handle_request(req);
        REQUIRE_FALSE(fsm.feed(d20::Event::V2GTP_MESSAGE).transitioned());
        REQUIRE(std::count(signals.begin(), signals.end(), session::feedback::Signal::AC_CLOSE_CONTACTOR) == 1);

        message_20::SessionStopRequest stop;
        stop.header.session_id = ctx.session.get_id();
        stop.header.timestamp = 1691411799;
        stop.charging_session = dt::ChargingSession::Terminate;
        state_helper.handle_request(stop);
        REQUIRE_FALSE(fsm.feed(d20::Event::V2GTP_MESSAGE).transitioned());

        THEN("it is a sequence error, the contactor is released and its timeout no longer answers the saved request") {
            const auto res = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res->response_code == dt::ResponseCode::FAILED_SequenceError);
            REQUIRE(ctx.session_stopped);
            REQUIRE(std::count(signals.begin(), signals.end(), session::feedback::Signal::AC_OPEN_CONTACTOR) == 1);

            std::this_thread::sleep_for(WAIT_OUT_CONTACTOR_TIMEOUT);
            REQUIRE(state_helper.get_timeouts().check().empty());
        }
    }

    GIVEN("A SessionStopReq(Terminate) with an EVTerminationCode before PowerDeliveryReq(Start)") {
        message_20::SessionStopRequest stop;
        stop.header.session_id = ctx.session.get_id();
        stop.header.timestamp = 1691411799;
        stop.charging_session = dt::ChargingSession::Terminate;
        stop.ev_termination_code = "EV_ABORT";
        state_helper.handle_request(stop);
        REQUIRE_FALSE(fsm.feed(d20::Event::V2GTP_MESSAGE).transitioned());

        THEN("the session ends and the code reaches the module") {
            const auto res = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(res.has_value());
            REQUIRE(res->response_code == dt::ResponseCode::OK);
            REQUIRE(ctx.session_stopped);
            REQUIRE(ev_termination_codes == std::vector<std::string>{"EV_ABORT"});
        }
    }
}
