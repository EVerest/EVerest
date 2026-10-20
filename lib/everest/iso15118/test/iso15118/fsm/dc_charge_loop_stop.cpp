// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include "helper.hpp"

#include <iso15118/d20/state/dc_charge_loop.hpp>
#include <iso15118/message/power_delivery.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;
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

bool contains(const std::vector<Signal>& signals, Signal signal) {
    return std::find(signals.begin(), signals.end(), signal) != signals.end();
}

} // namespace

// IEC 61851-23:2023 Table CC.9: side B is disabled (t103) before the PowerDeliveryRes (t104).
SCENARIO("ISO15118-20 DC charge loop holds PowerDeliveryRes(Stop) until the contactor is reported open") {
    const std::vector<d20::ControlMobilityNeedsModes> control_mobility_modes = {
        {dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc}};
    const d20::EvseSetupConfig evse_setup{"everest se",
                                          {dt::ServiceCategory::DC},
                                          {dt::Authorization::EIM},
                                          {},
                                          false,
                                          {},
                                          {},
                                          control_mobility_modes,
                                          std::nullopt,
                                          std::nullopt,
                                          std::nullopt,
                                          {}};
    std::optional<d20::PauseContext> pause_ctx;

    std::vector<Signal> signals;
    session::feedback::Callbacks callbacks{};
    callbacks.signal = [&signals](Signal signal) { signals.push_back(signal); };

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto& ctx = state_helper.get_context();
    ctx.session = d20::Session(
        d20::SelectedServiceParameters(dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
                                       dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing));

    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

    GIVEN("The EV sends PowerDeliveryReq(Stop) while the contactor is reported closed") {
        state_helper.set_active_control_event(d20::ClosedContactor{true});
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        state_helper.handle_request(make_power_delivery_stop(ctx.session));
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("The contactor is opened and welding detection starts, but no response is sent yet") {
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);
            REQUIRE(contains(signals, Signal::CHARGE_LOOP_FINISHED));
            REQUIRE(contains(signals, Signal::DC_OPEN_CONTACTOR));
            REQUIRE(contains(signals, Signal::WELDING_DETECTION_STARTED));
            REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
        }

        WHEN("The board support reports the contactor open") {
            state_helper.set_active_control_event(d20::ClosedContactor{false});
            const auto result = fsm.feed(d20::Event::CONTROL_MESSAGE);

            THEN("The response is sent and welding detection follows") {
                REQUIRE(result.transitioned());
                REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_WeldingDetection);
                const auto res = ctx.get_response<message_20::PowerDeliveryResponse>();
                REQUIRE(res.has_value());
                REQUIRE(res->response_code == dt::ResponseCode::OK);
            }
        }

        WHEN("A contactor-closed report arrives") {
            state_helper.set_active_control_event(d20::ClosedContactor{true});
            fsm.feed(d20::Event::CONTROL_MESSAGE);

            THEN("The response stays held") {
                REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);
                REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
            }
        }

        WHEN("The contactor is not reported open in time") {
            ctx.set_active_timeout(d20::TimeoutType::CONTACTOR);
            const auto result = fsm.feed(d20::Event::TIMEOUT);

            THEN("The response is sent anyway") {
                REQUIRE(result.transitioned());
                REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_WeldingDetection);
                REQUIRE(ctx.get_response<message_20::PowerDeliveryResponse>().has_value());
            }
        }
    }

    GIVEN("The contactor was already reported open") {
        state_helper.set_active_control_event(d20::ClosedContactor{false});
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        state_helper.handle_request(make_power_delivery_stop(ctx.session));
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("PowerDeliveryRes(Stop) is sent right away") {
            REQUIRE(result.transitioned());
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_WeldingDetection);
            REQUIRE(contains(signals, Signal::DC_OPEN_CONTACTOR));
            REQUIRE(ctx.get_response<message_20::PowerDeliveryResponse>().has_value());
        }
    }
}
