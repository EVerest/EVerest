// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/dc_cable_check.hpp>
#include <iso15118/d20/state/power_delivery.hpp>
#include <iso15118/d20/state/schedule_exchange.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

using Scheduled_ModeReq = message_20::datatypes::Scheduled_SEReqControlMode;
using Scheduled_ModeRes = message_20::datatypes::Scheduled_SEResControlMode;
using Dynamic_ModeReq = message_20::datatypes::Dynamic_SEReqControlMode;
using Dynamic_ModeRes = message_20::datatypes::Dynamic_SEResControlMode;

SCENARIO("ISO15118-20 schedule exchange state transitions") {

    const auto evse_id = std::string("everest se");
    const std::vector<dt::ServiceCategory> supported_energy_services = {dt::ServiceCategory::DC};
    const auto cert_install = false;
    const std::vector<dt::Authorization> auth_services = {dt::Authorization::EIM};
    const std::vector<uint16_t> vas_services{};

    d20::DcTransferLimits dc_limits;
    d20::AcTransferLimits ac_limits;
    d20::DcTransferLimits powersupply_limits;
    dc_limits.charge_limits.power.max = {22, 3};
    dc_limits.charge_limits.power.min = {10, 0};
    dc_limits.charge_limits.current.max = {250, 0};
    dc_limits.voltage.max = {900, 0};

    const std::vector<d20::ControlMobilityNeedsModes> control_mobility_modes = {
        {dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc},
        {dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc},
        {dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedBySecc}};

    const d20::EvseSetupConfig evse_setup{
        evse_id,   supported_energy_services, auth_services, vas_services, cert_install, dc_limits,
        ac_limits, control_mobility_modes,    std::nullopt,  std::nullopt, std::nullopt, powersupply_limits};

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();

    GIVEN("Bad case - Unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ScheduleExchange>()};

        ctx.session_config.supported_energy_transfer_services = supported_energy_services;
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::ScheduleExchangeRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;

        req.control_mode.emplace<Scheduled_ModeReq>();

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ScheduleExchange);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::ScheduleExchangeResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);

            REQUIRE(res.processing == dt::Processing::Finished);

            REQUIRE(std::holds_alternative<Dynamic_ModeRes>(res.control_mode) == true);
            const auto& res_control_mode = std::get<Dynamic_ModeRes>(res.control_mode);
            REQUIRE(std::holds_alternative<std::monostate>(res_control_mode.price_schedule) == true);
        }
    }

    GIVEN("Bad case - False control mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ScheduleExchange>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        message_20::ScheduleExchangeRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        req.control_mode.emplace<Dynamic_ModeReq>();

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ScheduleExchange);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::ScheduleExchangeResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);

            REQUIRE(res.processing == dt::Processing::Finished);

            REQUIRE(std::holds_alternative<Dynamic_ModeRes>(res.control_mode) == true);
            const auto& res_control_mode = std::get<Dynamic_ModeRes>(res.control_mode);
            REQUIRE(std::holds_alternative<std::monostate>(res_control_mode.price_schedule) == true);
        }
    }

    GIVEN("Good case - Scheduled Mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ScheduleExchange>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        // Set max power via powersupply_limits
        ctx.session_config.powersupply_limits.charge_limits.power.max = {22, 3};

        message_20::ScheduleExchangeRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        req.control_mode.emplace<Scheduled_ModeReq>();

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_CableCheck);

            const auto response_message = ctx.get_response<message_20::ScheduleExchangeResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);

            REQUIRE(res.processing == dt::Processing::Finished);

            REQUIRE(std::holds_alternative<Scheduled_ModeRes>(res.control_mode) == true);
            const auto& res_control_mode = std::get<Scheduled_ModeRes>(res.control_mode);

            REQUIRE(res_control_mode.schedule_tuple.size() == 1);
            const auto& schedule_tuple = res_control_mode.schedule_tuple[0];

            REQUIRE(dt::from_RationalNumber(schedule_tuple.charging_schedule.power_schedule.entries.at(0).power) ==
                    22000);
        }
    }

    GIVEN("Good case - Dynamic Mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ScheduleExchange>()};

        d20::SelectedServiceParameters service_parameters =
            d20::SelectedServiceParameters(dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Dynamic,
                                           dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        message_20::ScheduleExchangeRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        req.control_mode.emplace<Dynamic_ModeReq>();

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_CableCheck);

            const auto response_message = ctx.get_response<message_20::ScheduleExchangeResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.processing == dt::Processing::Finished);

            REQUIRE(std::holds_alternative<Dynamic_ModeRes>(res.control_mode) == true);
        }
    }

    GIVEN("Good case - MCS Dynamic Mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ScheduleExchange>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::MCS, dt::DcConnector::Extended, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        message_20::ScheduleExchangeRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        req.control_mode.emplace<Dynamic_ModeReq>();

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_CableCheck);

            const auto response_message = ctx.get_response<message_20::ScheduleExchangeResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.processing == dt::Processing::Finished);

            REQUIRE(std::holds_alternative<Dynamic_ModeRes>(res.control_mode) == true);
        }
    }

    // TODO(SL): Adding tests for ac charger
}
