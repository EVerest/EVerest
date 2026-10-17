// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
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

    const auto control = [&](const d20::ControlEvent& event) {
        state_helper.set_active_control_event(event);
        return fsm.feed(d20::Event::CONTROL_MESSAGE);
    };

    const auto require_stop_res_sent = [&](const auto& result) {
        REQUIRE(result.transitioned());
        REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_WeldingDetection);
        const auto res = ctx.get_response<message_20::PowerDeliveryResponse>();
        REQUIRE(res.has_value());
        REQUIRE(res->response_code == dt::ResponseCode::OK);
    };

    GIVEN("The EV sends PowerDeliveryReq(Stop) while the power path is on") {
        control(d20::ClosedContactor{true});
        state_helper.handle_request(make_power_delivery(ctx.session, dt::Progress::Stop));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The contactor is opened before any response is sent") {
            REQUIRE_FALSE(result.transitioned());
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);
            REQUIRE(contains(signals, Signal::CHARGE_LOOP_FINISHED));
            REQUIRE(contains(signals, Signal::DC_OPEN_CONTACTOR));
            REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
        }

        WHEN("The board support reports the contactor open and then the DC output is disabled") {
            REQUIRE_FALSE(control(d20::ClosedContactor{false}).transitioned());
            REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
            const auto off_result = control(d20::DcOutputDisabled{});

            THEN("The response is sent and welding detection follows") {
                require_stop_res_sent(off_result);
            }
        }

        WHEN("The DC output is disabled and then the board support reports the contactor open") {
            REQUIRE_FALSE(control(d20::DcOutputDisabled{}).transitioned());
            REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
            const auto off_result = control(d20::ClosedContactor{false});

            THEN("The response is sent and welding detection follows") {
                require_stop_res_sent(off_result);
            }
        }

        WHEN("A contactor-closed report arrives after the DC output is disabled") {
            control(d20::DcOutputDisabled{});
            const auto closed_result = control(d20::ClosedContactor{true});

            THEN("The response stays held") {
                REQUIRE_FALSE(closed_result.transitioned());
                REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
            }
        }

        WHEN("The power path is not reported off in time") {
            ctx.set_active_timeout(d20::TimeoutType::CONTACTOR);
            const auto timeout_result = fsm.feed(d20::Event::TIMEOUT);

            THEN("The response is sent anyway") {
                require_stop_res_sent(timeout_result);
            }
        }
    }

    GIVEN("The contactor was already reported open") {
        control(d20::ClosedContactor{false});

        state_helper.handle_request(make_power_delivery(ctx.session, dt::Progress::Stop));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("PowerDeliveryRes(Stop) still waits for the DC output to be disabled") {
            REQUIRE_FALSE(result.transitioned());
            REQUIRE(contains(signals, Signal::DC_OPEN_CONTACTOR));
            REQUIRE_FALSE(state_helper.get_message_exchange().has_response());

            require_stop_res_sent(control(d20::DcOutputDisabled{}));
        }
    }

    GIVEN("A DcOutputDisabled from before the stop request") {
        control(d20::ClosedContactor{false});
        control(d20::DcOutputDisabled{});

        state_helper.handle_request(make_power_delivery(ctx.session, dt::Progress::Stop));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("It does not release the response") {
            REQUIRE_FALSE(result.transitioned());
            REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
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

            state_helper.set_active_control_event(d20::DcOutputDisabled{});
            REQUIRE_FALSE(mcs_fsm.feed(d20::Event::CONTROL_MESSAGE).transitioned());
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
