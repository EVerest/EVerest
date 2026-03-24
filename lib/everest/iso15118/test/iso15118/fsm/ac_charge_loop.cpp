// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/ac_charge_loop.hpp>
#include <iso15118/d20/state/power_delivery.hpp>
#include <iso15118/d20/state/session_stop.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

using Scheduled_AC_Req = dt::Scheduled_AC_CLReqControlMode;
using Scheduled_BPT_AC_Req = dt::BPT_Scheduled_AC_CLReqControlMode;
using Dynamic_AC_Req = dt::Dynamic_AC_CLReqControlMode;
using Dynamic_BPT_AC_Req = dt::BPT_Dynamic_AC_CLReqControlMode;

using Scheduled_AC_Res = dt::Scheduled_AC_CLResControlMode;
using Scheduled_BPT_AC_Res = dt::BPT_Scheduled_AC_CLResControlMode;
using Dynamic_AC_Res = dt::Dynamic_AC_CLResControlMode;
using Dynamic_BPT_AC_Res = dt::BPT_Dynamic_AC_CLResControlMode;

SCENARIO("ISO15118-20 ac charge loop state transitions") {

    const auto evse_id = std::string("everest se");
    const std::vector<dt::ServiceCategory> supported_energy_services = {dt::ServiceCategory::AC,
                                                                        dt::ServiceCategory::AC_BPT};
    const auto cert_install = false;
    const std::vector<dt::Authorization> auth_services = {dt::Authorization::EIM};
    const std::vector<uint16_t> vas_services{};

    d20::DcTransferLimits dc_limits;
    d20::DcTransferLimits powersupply_limits;
    d20::AcTransferLimits ac_limits;
    ac_limits.charge_power = {{22, 3}, {10, 0}};
    ac_limits.nominal_frequency = {50, 0};

    auto& discharge_limits = ac_limits.discharge_power.emplace();
    discharge_limits.max = {11, 3};
    discharge_limits.min = {10, 0};

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
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeLoop>()};

        ctx.session_config.supported_energy_transfer_services = supported_energy_services;
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::AC_ChargeLoopRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Scheduled_AC_Req>();
        req_control_mode.present_active_power = {0, 0};

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeLoop);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE(res.target_frequency.has_value() == false);
            REQUIRE(std::holds_alternative<Scheduled_AC_Res>(res.control_mode));
        }
    }

    GIVEN("Bad case - false energy mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC_BPT, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing, 230, dt::GridCodeIslandingDetectionMethod::Passive);

        ctx.session = d20::Session(service_parameters);

        message_20::AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Scheduled_AC_Req>();
        req_control_mode.present_active_power = {0, 0};

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeLoop);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE(res.target_frequency.has_value() == false);
            REQUIRE(std::holds_alternative<Scheduled_AC_Res>(res.control_mode));
        }
    }

    GIVEN("Bad case - false control mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230);

        ctx.session = d20::Session(service_parameters);

        message_20::AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Scheduled_AC_Req>();
        req_control_mode.present_active_power = {0, 0};

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeLoop);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE(res.target_frequency.has_value() == false);
            REQUIRE(std::holds_alternative<Scheduled_AC_Res>(res.control_mode));
        }
    }

    GIVEN("Good case - AC scheduled mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230);

        ctx.session = d20::Session(service_parameters);

        // Set control events for target and present power
        d20::AcTargetPower target_power{};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        d20::AcPresentPower present_power{};
        present_power.present_active_power = {11, 3};
        state_helper.set_active_control_event(present_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Scheduled_AC_Req>();
        req_control_mode.present_active_power = {11, 3};

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE(dt::from_RationalNumber(res.target_frequency.value_or(dt::RationalNumber{0, 0})) == 50.0f);
            REQUIRE(std::holds_alternative<Scheduled_AC_Res>(res.control_mode));

            const auto& res_control_mode = std::get<Scheduled_AC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.present_active_power.value_or(dt::RationalNumber{0, 0})) ==
                    11000.0f);
        }
    }

    GIVEN("Good case - AC_BPT scheduled mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC_BPT, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing, 230, dt::GridCodeIslandingDetectionMethod::Passive);

        ctx.session = d20::Session(service_parameters);

        // Set control events for target and present power
        d20::AcTargetPower target_power{};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        d20::AcPresentPower present_power{};
        present_power.present_active_power = {11, 3};
        state_helper.set_active_control_event(present_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Scheduled_BPT_AC_Req>();
        req_control_mode.present_active_power = {0, 0};

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE(dt::from_RationalNumber(res.target_frequency.value_or(dt::RationalNumber{0, 0})) == 50.0f);
            REQUIRE(std::holds_alternative<Scheduled_BPT_AC_Res>(res.control_mode));

            const auto& res_control_mode = std::get<Scheduled_BPT_AC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.present_active_power.value_or(dt::RationalNumber{0, 0})) ==
                    11000.0f);
        }
    }

    GIVEN("Good case - AC dynamic mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230);

        ctx.session = d20::Session(service_parameters);

        // Set control events for target and present power
        d20::AcTargetPower target_power{};
        target_power.target_active_power = {11, 3};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        d20::AcPresentPower present_power{};
        present_power.present_active_power = {11, 3};
        state_helper.set_active_control_event(present_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Dynamic_AC_Req>();
        req_control_mode.present_active_power = {11, 3};
        req_control_mode.max_charge_power = {11, 3};
        req_control_mode.min_charge_power = {4, 0};
        req_control_mode.present_reactive_power = {10, 0};

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE(dt::from_RationalNumber(res.target_frequency.value_or(dt::RationalNumber{0, 0})) == 50.0f);
            REQUIRE(std::holds_alternative<Dynamic_AC_Res>(res.control_mode));

            const auto& res_control_mode = std::get<Dynamic_AC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.present_active_power.value_or(dt::RationalNumber{0, 0})) ==
                    11000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.target_active_power) == 11000.0f);
        }
    }

    GIVEN("Good case - AC_BPT dynamic mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC_BPT, dt::AcConnector::ThreePhase, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing, 230, dt::GridCodeIslandingDetectionMethod::Passive);

        ctx.session = d20::Session(service_parameters);

        // Set control events for target and present power
        d20::AcTargetPower target_power{};
        target_power.target_active_power = {11, 3};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        d20::AcPresentPower present_power{};
        present_power.present_active_power = {11, 3};
        state_helper.set_active_control_event(present_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Dynamic_BPT_AC_Req>();
        req_control_mode.present_active_power = {11, 3};
        req_control_mode.max_charge_power = {11, 3};
        req_control_mode.min_charge_power = {4, 0};
        req_control_mode.present_reactive_power = {10, 0};

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE(dt::from_RationalNumber(res.target_frequency.value_or(dt::RationalNumber{0, 0})) == 50.0f);
            REQUIRE(std::holds_alternative<Dynamic_BPT_AC_Res>(res.control_mode));

            const auto& res_control_mode = std::get<Dynamic_BPT_AC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.present_active_power.value_or(dt::RationalNumber{0, 0})) ==
                    11000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.target_active_power) == 11000.0f);
        }
    }

    GIVEN("Good case - AC dynamic mode, mobility_needs_mode = 2") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedBySecc, dt::Pricing::NoPricing, 230);

        ctx.session = d20::Session(service_parameters);

        // Set control events for target, present power, and dynamic parameters
        d20::AcTargetPower target_power{};
        target_power.target_active_power = {11, 3};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        d20::AcPresentPower present_power{};
        present_power.present_active_power = {11, 3};
        state_helper.set_active_control_event(present_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        const d20::UpdateDynamicModeParameters dynamic_parameters = {std::time(nullptr) + 40, std::nullopt, 95};
        state_helper.set_active_control_event(dynamic_parameters);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Dynamic_AC_Req>();
        req_control_mode.present_active_power = {11, 3};
        req_control_mode.max_charge_power = {11, 3};
        req_control_mode.min_charge_power = {4, 0};
        req_control_mode.present_reactive_power = {10, 0};

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE(dt::from_RationalNumber(res.target_frequency.value_or(dt::RationalNumber{0, 0})) == 50.0f);
            REQUIRE(std::holds_alternative<Dynamic_AC_Res>(res.control_mode));

            const auto& res_control_mode = std::get<Dynamic_AC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.present_active_power.value_or(dt::RationalNumber{0, 0})) ==
                    11000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.target_active_power) == 11000.0f);

            REQUIRE(res_control_mode.departure_time.value_or(0) >= 39);
            REQUIRE(res_control_mode.minimum_soc.value_or(0) == 95);
            REQUIRE(res_control_mode.ack_max_delay.value_or(0) == 30);
        }
    }

    GIVEN("Good case - AC_BPT dynamic mode, mobility_needs_mode = 2") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC_BPT, dt::AcConnector::ThreePhase, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedBySecc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing, 230, dt::GridCodeIslandingDetectionMethod::Passive);

        ctx.session = d20::Session(service_parameters);

        // Set control events for target, present power, and dynamic parameters
        d20::AcTargetPower target_power{};
        target_power.target_active_power = {11, 3};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        d20::AcPresentPower present_power{};
        present_power.present_active_power = {11, 3};
        state_helper.set_active_control_event(present_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        const d20::UpdateDynamicModeParameters dynamic_parameters = {std::time(nullptr) + 40, std::nullopt, 95};
        state_helper.set_active_control_event(dynamic_parameters);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Dynamic_BPT_AC_Req>();
        req_control_mode.present_active_power = {11, 3};
        req_control_mode.max_charge_power = {11, 3};
        req_control_mode.min_charge_power = {4, 0};
        req_control_mode.present_reactive_power = {10, 0};

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE(dt::from_RationalNumber(res.target_frequency.value_or(dt::RationalNumber{0, 0})) == 50.0f);
            REQUIRE(std::holds_alternative<Dynamic_BPT_AC_Res>(res.control_mode));

            const auto& res_control_mode = std::get<Dynamic_BPT_AC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.present_active_power.value_or(dt::RationalNumber{0, 0})) ==
                    11000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.target_active_power) == 11000.0f);

            REQUIRE(res_control_mode.departure_time.value_or(0) >= 39);
            REQUIRE(res_control_mode.minimum_soc.value_or(0) == 95);
            REQUIRE(res_control_mode.ack_max_delay.value_or(0) == 30);
        }
    }

    GIVEN("Good case - AC dynamic mode & pause from charger") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230);

        ctx.session = d20::Session(service_parameters);

        // Set control events for pause, target and present power
        state_helper.set_active_control_event(d20::PauseCharging{true});
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        d20::AcTargetPower target_power{};
        target_power.target_active_power = {11, 3};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        d20::AcPresentPower present_power{};
        present_power.present_active_power = {11, 3};
        state_helper.set_active_control_event(present_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Dynamic_AC_Req>();
        req_control_mode.present_active_power = {11, 3};
        req_control_mode.max_charge_power = {11, 3};
        req_control_mode.min_charge_power = {4, 0};
        req_control_mode.present_reactive_power = {10, 0};

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE(dt::from_RationalNumber(res.target_frequency.value_or(dt::RationalNumber{0, 0})) == 50.0f);
            REQUIRE(std::holds_alternative<Dynamic_AC_Res>(res.control_mode));

            const auto& res_control_mode = std::get<Dynamic_AC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.present_active_power.value_or(dt::RationalNumber{0, 0})) ==
                    11000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.target_active_power) == 11000.0f);

            REQUIRE(res.status.has_value() == true);
            REQUIRE(res.status.value().notification == dt::EvseNotification::Pause);
            REQUIRE(res.status.value().notification_max_delay == 60);
        }
    }
}
