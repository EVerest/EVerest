// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include "helper.hpp"

#include <iso15118/d20/state/dc_charge_loop.hpp>
#include <iso15118/message/power_delivery.hpp>

using namespace iso15118;

using Signal = session::feedback::Signal;

namespace {

message_20::PowerDeliveryRequest make_power_delivery(const d20::Session& session, dt::Progress progress) {
    message_20::PowerDeliveryRequest req;
    req.header.session_id = session.get_id();
    req.header.timestamp = 1691411798;
    req.processing = dt::Processing::Finished;
    req.charge_progress = progress;
    return req;
}

bool contains(const std::vector<Signal>& signals, Signal signal) {
    return std::find(signals.begin(), signals.end(), signal) != signals.end();
}

} // namespace

SCENARIO("ISO15118-20 DC charge loop holds PowerDeliveryRes(Stop) until the power path is off") {
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

    GIVEN("The EV sends PowerDeliveryReq(Stop) while the power path is on") {
        state_helper.handle_request(make_power_delivery(ctx.session, dt::Progress::Stop));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The contactor is opened before any response is sent") {
            REQUIRE_FALSE(result.transitioned());
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);
            REQUIRE(contains(signals, Signal::CHARGE_LOOP_FINISHED));
            REQUIRE(contains(signals, Signal::DC_OPEN_CONTACTOR));
            REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
        }

        WHEN("The board support reports the power path off") {
            state_helper.set_active_control_event(d20::ClosedContactor{false});
            const auto off_result = fsm.feed(d20::Event::CONTROL_MESSAGE);

            THEN("The response is sent and welding detection follows") {
                REQUIRE(off_result.transitioned());
                REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_WeldingDetection);
                const auto res = ctx.get_response<message_20::PowerDeliveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }

        WHEN("A contactor-closed report arrives") {
            state_helper.set_active_control_event(d20::ClosedContactor{true});
            const auto closed_result = fsm.feed(d20::Event::CONTROL_MESSAGE);

            THEN("The response stays held") {
                REQUIRE_FALSE(closed_result.transitioned());
                REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
            }
        }

        WHEN("The power path is not reported off in time") {
            ctx.set_active_timeout(d20::TimeoutType::CONTACTOR);
            const auto timeout_result = fsm.feed(d20::Event::TIMEOUT);

            THEN("The response is sent anyway") {
                REQUIRE(timeout_result.transitioned());
                REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_WeldingDetection);
                const auto res = ctx.get_response<message_20::PowerDeliveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }
    }

    GIVEN("The power path was already reported off") {
        state_helper.set_active_control_event(d20::ClosedContactor{false});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        state_helper.handle_request(make_power_delivery(ctx.session, dt::Progress::Stop));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("PowerDeliveryRes(Stop) is sent right away") {
            REQUIRE(result.transitioned());
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_WeldingDetection);
            REQUIRE(contains(signals, Signal::DC_OPEN_CONTACTOR));
            REQUIRE(ctx.get_response<message_20::PowerDeliveryResponse>().has_value());
        }
    }

    GIVEN("An MCS session and PowerDeliveryReq(Stop)") {
        ctx.session = d20::Session(
            d20::SelectedServiceParameters(dt::ServiceCategory::MCS, dt::McsConnector::Mcs, dt::ControlMode::Scheduled,
                                           dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing));
        fsm::v2::FSM<d20::StateBase> mcs_fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        state_helper.handle_request(make_power_delivery(ctx.session, dt::Progress::Stop));
        mcs_fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The response waits for the power path to be off") {
            REQUIRE(contains(signals, Signal::DC_OPEN_CONTACTOR));
            REQUIRE_FALSE(state_helper.get_message_exchange().has_response());

            state_helper.set_active_control_event(d20::ClosedContactor{false});
            REQUIRE(mcs_fsm.feed(d20::Event::CONTROL_MESSAGE).transitioned());
            REQUIRE(mcs_fsm.get_current_state_id() == d20::StateID::DC_WeldingDetection);
            REQUIRE(ctx.get_response<message_20::PowerDeliveryResponse>().has_value());
        }
    }

    GIVEN("The EV sends PowerDeliveryReq(Standby)") {
        state_helper.handle_request(make_power_delivery(ctx.session, dt::Progress::Standby));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The response is sent immediately and the contactor stays closed") {
            REQUIRE_FALSE(result.transitioned());
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);
            REQUIRE_FALSE(contains(signals, Signal::DC_OPEN_CONTACTOR));
            REQUIRE(ctx.get_response<message_20::PowerDeliveryResponse>().has_value());
        }
    }
}
