// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include "helper.hpp"

#include <iso15118/d20/state/dc_cable_check.hpp>
#include <iso15118/d20/state/dc_charge_loop.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/dc_pre_charge.hpp>
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

message_20::PowerDeliveryRequest make_power_delivery_start(const d20::Session& session) {
    auto req = make_power_delivery_stop(session);
    req.charge_progress = dt::Progress::Start;
    return req;
}

message_20::DC_CableCheckRequest make_cable_check(const d20::Session& session) {
    message_20::DC_CableCheckRequest req;
    req.header.session_id = session.get_id();
    req.header.timestamp = 1691411798;
    return req;
}

message_20::DC_PreChargeRequest make_pre_charge(const d20::Session& session) {
    message_20::DC_PreChargeRequest req;
    req.header.session_id = session.get_id();
    req.header.timestamp = 1691411798;
    req.processing = dt::Processing::Ongoing;
    req.present_voltage = {400, 0};
    req.target_voltage = {400, 0};
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

    // The contactor closes during cable check, so DC_ChargeLoop never sees it being reported closed.
    GIVEN("The EV sends PowerDeliveryReq(Stop) and no contactor report reached the charge loop") {
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

// The order of a real session: EvseManager reports the contactor closed during cable check, long before
// DC_ChargeLoop exists, and reports it open only after DC_OPEN_CONTACTOR.
SCENARIO("ISO15118-20 DC session from cable check holds PowerDeliveryRes(Stop) until the contactor is open") {
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

    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_CableCheck>()};

    const auto control = [&](const d20::ControlEvent& event) {
        state_helper.set_active_control_event(event);
        return fsm.feed(d20::Event::CONTROL_MESSAGE);
    };
    const auto request = [&](const auto& req) {
        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);
        return result;
    };
    const auto clear_response = [&] { state_helper.get_message_exchange().check_and_clear_response(); };

    GIVEN("A session that reached the charge loop") {
        control(d20::ClosedContactor{true});
        control(d20::CableCheckFinished{true});
        request(make_cable_check(ctx.session));
        REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_PreCharge);
        clear_response();
        request(make_pre_charge(ctx.session));
        REQUIRE(fsm.get_current_state_id() == d20::StateID::PowerDelivery);
        clear_response();
        request(make_power_delivery_start(ctx.session));
        REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);
        clear_response();

        WHEN("The EV sends PowerDeliveryReq(Stop)") {
            request(make_power_delivery_stop(ctx.session));

            THEN("The contactor is asked to open and the response is held") {
                REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);
                REQUIRE(contains(signals, Signal::DC_OPEN_CONTACTOR));
                REQUIRE_FALSE(state_helper.get_message_exchange().has_response());
            }

            AND_WHEN("The board support reports the contactor open") {
                const auto result = control(d20::ClosedContactor{false});

                THEN("The response is sent and welding detection follows") {
                    REQUIRE(result.transitioned());
                    REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_WeldingDetection);
                    const auto res = ctx.get_response<message_20::PowerDeliveryResponse>();
                    REQUIRE(res.has_value());
                    REQUIRE(res->response_code == dt::ResponseCode::OK);
                }
            }
        }
    }
}
