// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/dc_charge_loop.hpp>
#include <iso15118/d20/state/dc_welding_detection.hpp>
#include <iso15118/d20/state/power_delivery.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

using Scheduled_DC_Req = message_20::datatypes::Scheduled_DC_CLReqControlMode;
using Scheduled_BPT_DC_Req = message_20::datatypes::BPT_Scheduled_DC_CLReqControlMode;
using Dynamic_DC_Req = message_20::datatypes::Dynamic_DC_CLReqControlMode;
using Dynamic_BPT_DC_Req = message_20::datatypes::BPT_Dynamic_DC_CLReqControlMode;

using Scheduled_DC_Res = message_20::datatypes::Scheduled_DC_CLResControlMode;
using Scheduled_BPT_DC_Res = message_20::datatypes::BPT_Scheduled_DC_CLResControlMode;
using Dynamic_DC_Res = message_20::datatypes::Dynamic_DC_CLResControlMode;
using Dynamic_BPT_DC_Res = message_20::datatypes::BPT_Dynamic_DC_CLResControlMode;

SCENARIO("ISO15118-20 dc charge loop state transitions") {

    const auto evse_id = std::string("everest se");
    const std::vector<dt::ServiceCategory> supported_energy_services = {
        dt::ServiceCategory::DC, dt::ServiceCategory::DC_BPT, dt::ServiceCategory::MCS,
        dt::ServiceCategory::MCS_BPT};
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
    auto& discharge_limits = dc_limits.discharge_limits.emplace();
    discharge_limits.power.max = {11, 3};
    discharge_limits.power.min = {10, 0};
    discharge_limits.current.max = {30, 0};

    const std::vector<d20::ControlMobilityNeedsModes> control_mobility_modes = {
        {dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc},
        {dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc},
        {dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedBySecc}};

    const d20::EvseSetupConfig evse_setup{
        evse_id, supported_energy_services, auth_services, vas_services, cert_install, dc_limits,
        ac_limits, control_mobility_modes, std::nullopt, std::nullopt, std::nullopt, powersupply_limits};

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();

    GIVEN("Bad case - Unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        ctx.session_config.supported_energy_transfer_services = supported_energy_services;
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Scheduled_DC_Req>();
        req_control_mode.target_current = {40, 0};
        req_control_mode.target_voltage = {400, 0};

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 0.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 0.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);
            REQUIRE(std::holds_alternative<Scheduled_DC_Res>(res.control_mode));
        }
    }

    GIVEN("Bad case - false energy mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::DC_BPT, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Scheduled_DC_Req>();
        req_control_mode.target_current = {40, 0};
        req_control_mode.target_voltage = {400, 0};

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 0.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 0.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);
            REQUIRE(std::holds_alternative<Scheduled_DC_Res>(res.control_mode));
        }
    }

    GIVEN("Bad case - false control mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters =
            d20::SelectedServiceParameters(dt::ServiceCategory::DC, dt::DcConnector::Extended,
                                           dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc,
                                           dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Scheduled_DC_Req>();
        req_control_mode.target_current = {40, 0};
        req_control_mode.target_voltage = {400, 0};

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 0.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 0.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);
            REQUIRE(std::holds_alternative<Scheduled_DC_Res>(res.control_mode));
        }
    }

    GIVEN("Good case - DC scheduled mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters =
            d20::SelectedServiceParameters(dt::ServiceCategory::DC, dt::DcConnector::Extended,
                                           dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc,
                                           dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Scheduled_DC_Req>();
        req_control_mode.target_current = {40, 0};
        req_control_mode.target_voltage = {400, 0};

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 30.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 330.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);

            REQUIRE(std::holds_alternative<Scheduled_DC_Res>(res.control_mode));
            const auto& res_control_mode = std::get<Scheduled_DC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_charge_power.value_or(dt::RationalNumber{0, 0})) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.min_charge_power.value_or(dt::RationalNumber{0, 0})) == 10.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_charge_current.value_or(dt::RationalNumber{0, 0})) == 250.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_voltage.value_or(dt::RationalNumber{0, 0})) == 900.0f);
        }
    }

    GIVEN("Good case - DC_BPT scheduled mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::DC_BPT, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Scheduled_BPT_DC_Req>();
        req_control_mode.target_current = {40, 0};
        req_control_mode.target_voltage = {400, 0};
        req_control_mode.max_discharge_power.emplace<dt::RationalNumber>({11, 3});

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 30.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 330.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);

            REQUIRE(std::holds_alternative<Scheduled_BPT_DC_Res>(res.control_mode));
            const auto& res_control_mode = std::get<Scheduled_BPT_DC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_charge_power.value_or(dt::RationalNumber{0, 0})) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.min_charge_power.value_or(dt::RationalNumber{0, 0})) == 10.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_charge_current.value_or(dt::RationalNumber{0, 0})) == 250.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_voltage.value_or(dt::RationalNumber{0, 0})) == 900.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_discharge_power.value_or(dt::RationalNumber{0, 0})) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.min_discharge_power.value_or(dt::RationalNumber{0, 0})) == 10.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_discharge_current.value_or(dt::RationalNumber{0, 0})) == 30.0f);
        }
    }

    GIVEN("Good case - DC dynamic mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters =
            d20::SelectedServiceParameters(dt::ServiceCategory::DC, dt::DcConnector::Extended,
                                           dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc,
                                           dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Dynamic_DC_Req>();
        req_control_mode.target_energy_request = {68, 3};
        req_control_mode.max_energy_request = {70, 3};
        req_control_mode.min_energy_request = {40, 3};
        req_control_mode.max_charge_power = {30, 3};
        req_control_mode.min_charge_power = {27, 2};
        req_control_mode.max_charge_current = {400, 0};
        req_control_mode.max_voltage = {950, 0};
        req_control_mode.min_voltage = {150, 0};

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 30.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 330.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);

            REQUIRE(std::holds_alternative<Dynamic_DC_Res>(res.control_mode));
            const auto& res_control_mode = std::get<Dynamic_DC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_power) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.min_charge_power) == 10.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_current) == 250.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_voltage) == 900.0f);
        }
    }

    GIVEN("Good case - DC_BPT dynamic mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::DC_BPT, dt::DcConnector::Extended, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Dynamic_BPT_DC_Req>();
        req_control_mode.target_energy_request = {68, 3};
        req_control_mode.max_energy_request = {70, 3};
        req_control_mode.min_energy_request = {40, 3};
        req_control_mode.max_charge_power = {30, 3};
        req_control_mode.min_charge_power = {27, 2};
        req_control_mode.max_charge_current = {400, 0};
        req_control_mode.max_voltage = {950, 0};
        req_control_mode.min_voltage = {150, 0};
        req_control_mode.max_discharge_power = {11, 3};
        req_control_mode.min_discharge_power = {10, 0};
        req_control_mode.max_discharge_current = {30, 0};

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 30.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 330.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);

            REQUIRE(std::holds_alternative<Dynamic_BPT_DC_Res>(res.control_mode));
            const auto& res_control_mode = std::get<Dynamic_BPT_DC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_power) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.min_charge_power) == 10.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_current) == 250.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_voltage) == 900.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_discharge_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.min_discharge_power) == 10.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_discharge_current) == 30.0f);
        }
    }

    GIVEN("Good case - DC dynamic mode, mobility_needs_mode = 2") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters =
            d20::SelectedServiceParameters(dt::ServiceCategory::DC, dt::DcConnector::Extended,
                                           dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedBySecc,
                                           dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        // Set dynamic mode parameters
        state_helper.set_active_control_event(
            d20::UpdateDynamicModeParameters{std::time(nullptr) + 60, 95, std::nullopt});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Dynamic_DC_Req>();
        req_control_mode.target_energy_request = {68, 3};
        req_control_mode.max_energy_request = {70, 3};
        req_control_mode.min_energy_request = {40, 3};
        req_control_mode.max_charge_power = {30, 3};
        req_control_mode.min_charge_power = {27, 2};
        req_control_mode.max_charge_current = {400, 0};
        req_control_mode.max_voltage = {950, 0};
        req_control_mode.min_voltage = {150, 0};

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 30.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 330.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);

            REQUIRE(std::holds_alternative<Dynamic_DC_Res>(res.control_mode));
            const auto& res_control_mode = std::get<Dynamic_DC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_power) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.min_charge_power) == 10.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_current) == 250.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_voltage) == 900.0f);

            REQUIRE(res_control_mode.departure_time.value_or(0) >= 59);
            REQUIRE(res_control_mode.target_soc.value_or(0) == 95);
            REQUIRE(res_control_mode.ack_max_delay.value_or(0) == 30);
        }
    }

    GIVEN("Good case - DC_BPT dynamic mode, mobility_needs_mode = 2") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::DC_BPT, dt::DcConnector::Extended, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedBySecc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        // Set dynamic mode parameters
        state_helper.set_active_control_event(
            d20::UpdateDynamicModeParameters{std::time(nullptr) + 40, std::nullopt, 95});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Dynamic_BPT_DC_Req>();
        req_control_mode.target_energy_request = {68, 3};
        req_control_mode.max_energy_request = {70, 3};
        req_control_mode.min_energy_request = {40, 3};
        req_control_mode.max_charge_power = {30, 3};
        req_control_mode.min_charge_power = {27, 2};
        req_control_mode.max_charge_current = {400, 0};
        req_control_mode.max_voltage = {950, 0};
        req_control_mode.min_voltage = {150, 0};
        req_control_mode.max_discharge_power = {11, 3};
        req_control_mode.min_discharge_power = {10, 0};
        req_control_mode.max_discharge_current = {30, 0};

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 30.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 330.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);

            REQUIRE(std::holds_alternative<Dynamic_BPT_DC_Res>(res.control_mode));
            const auto& res_control_mode = std::get<Dynamic_BPT_DC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_power) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.min_charge_power) == 10.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_current) == 250.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_voltage) == 900.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_discharge_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.min_discharge_power) == 10.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_discharge_current) == 30.0f);

            REQUIRE(res_control_mode.departure_time.value_or(0) >= 39);
            REQUIRE(res_control_mode.minimum_soc.value_or(0) == 95);
            REQUIRE(res_control_mode.ack_max_delay.value_or(0) == 30);
        }
    }

    GIVEN("Good case - DC dynamic mode & pause from charger") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters =
            d20::SelectedServiceParameters(dt::ServiceCategory::DC, dt::DcConnector::Extended,
                                           dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc,
                                           dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        // Set pause flag
        state_helper.set_active_control_event(d20::PauseCharging{true});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Dynamic_DC_Req>();
        req_control_mode.target_energy_request = {68, 3};
        req_control_mode.max_energy_request = {70, 3};
        req_control_mode.min_energy_request = {40, 3};
        req_control_mode.max_charge_power = {30, 3};
        req_control_mode.min_charge_power = {27, 2};
        req_control_mode.max_charge_current = {400, 0};
        req_control_mode.max_voltage = {950, 0};
        req_control_mode.min_voltage = {150, 0};

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 30.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 330.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);

            REQUIRE(std::holds_alternative<Dynamic_DC_Res>(res.control_mode));
            const auto& res_control_mode = std::get<Dynamic_DC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_power) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.min_charge_power) == 10.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_current) == 250.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_voltage) == 900.0f);

            REQUIRE(res.status.has_value() == true);
            REQUIRE(res.status.value().notification == dt::EvseNotification::Pause);
            REQUIRE(res.status.value().notification_max_delay == 60);
        }
    }

    GIVEN("Good case - MCS scheduled mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters =
            d20::SelectedServiceParameters(dt::ServiceCategory::MCS, dt::DcConnector::Extended,
                                           dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc,
                                           dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Scheduled_DC_Req>();
        req_control_mode.target_current = {40, 0};
        req_control_mode.target_voltage = {400, 0};

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 30.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 330.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);

            REQUIRE(std::holds_alternative<Scheduled_DC_Res>(res.control_mode));
            const auto& res_control_mode = std::get<Scheduled_DC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_charge_power.value_or(dt::RationalNumber{0, 0})) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.min_charge_power.value_or(dt::RationalNumber{0, 0})) == 10.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_charge_current.value_or(dt::RationalNumber{0, 0})) == 250.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_voltage.value_or(dt::RationalNumber{0, 0})) == 900.0f);
        }
    }

    GIVEN("Good case - MCS_BPT scheduled mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::MCS_BPT, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Scheduled_BPT_DC_Req>();
        req_control_mode.target_current = {40, 0};
        req_control_mode.target_voltage = {400, 0};
        req_control_mode.max_discharge_power.emplace<dt::RationalNumber>({11, 3});

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 30.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 330.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);

            REQUIRE(std::holds_alternative<Scheduled_BPT_DC_Res>(res.control_mode));
            const auto& res_control_mode = std::get<Scheduled_BPT_DC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_charge_power.value_or(dt::RationalNumber{0, 0})) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.min_charge_power.value_or(dt::RationalNumber{0, 0})) == 10.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_charge_current.value_or(dt::RationalNumber{0, 0})) == 250.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_voltage.value_or(dt::RationalNumber{0, 0})) == 900.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_discharge_power.value_or(dt::RationalNumber{0, 0})) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.min_discharge_power.value_or(dt::RationalNumber{0, 0})) == 10.0f);
            REQUIRE(dt::from_RationalNumber(
                        res_control_mode.max_discharge_current.value_or(dt::RationalNumber{0, 0})) == 30.0f);
        }
    }

    GIVEN("Good case - MCS dynamic mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters =
            d20::SelectedServiceParameters(dt::ServiceCategory::MCS, dt::DcConnector::Extended,
                                           dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc,
                                           dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Dynamic_DC_Req>();
        req_control_mode.target_energy_request = {68, 3};
        req_control_mode.max_energy_request = {70, 3};
        req_control_mode.min_energy_request = {40, 3};
        req_control_mode.max_charge_power = {30, 3};
        req_control_mode.min_charge_power = {27, 2};
        req_control_mode.max_charge_current = {400, 0};
        req_control_mode.max_voltage = {950, 0};
        req_control_mode.min_voltage = {150, 0};

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 30.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 330.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);

            REQUIRE(std::holds_alternative<Dynamic_DC_Res>(res.control_mode));
            const auto& res_control_mode = std::get<Dynamic_DC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_power) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.min_charge_power) == 10.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_current) == 250.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_voltage) == 900.0f);
        }
    }

    GIVEN("Good case - MCS_BPT dynamic mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::MCS_BPT, dt::DcConnector::Extended, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Dynamic_BPT_DC_Req>();
        req_control_mode.target_energy_request = {68, 3};
        req_control_mode.max_energy_request = {70, 3};
        req_control_mode.min_energy_request = {40, 3};
        req_control_mode.max_charge_power = {30, 3};
        req_control_mode.min_charge_power = {27, 2};
        req_control_mode.max_charge_current = {400, 0};
        req_control_mode.max_voltage = {950, 0};
        req_control_mode.min_voltage = {150, 0};
        req_control_mode.max_discharge_power = {11, 3};
        req_control_mode.min_discharge_power = {10, 0};
        req_control_mode.max_discharge_current = {30, 0};

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 30.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 330.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);

            REQUIRE(std::holds_alternative<Dynamic_BPT_DC_Res>(res.control_mode));
            const auto& res_control_mode = std::get<Dynamic_BPT_DC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_power) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.min_charge_power) == 10.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_current) == 250.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_voltage) == 900.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_discharge_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.min_discharge_power) == 10.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_discharge_current) == 30.0f);
        }
    }

    GIVEN("Good case - MCS dynamic mode, mobility_needs_mode = 2") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters =
            d20::SelectedServiceParameters(dt::ServiceCategory::MCS, dt::DcConnector::Extended,
                                           dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedBySecc,
                                           dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        // Set dynamic mode parameters
        state_helper.set_active_control_event(
            d20::UpdateDynamicModeParameters{std::time(nullptr) + 60, 95, std::nullopt});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Dynamic_DC_Req>();
        req_control_mode.target_energy_request = {68, 3};
        req_control_mode.max_energy_request = {70, 3};
        req_control_mode.min_energy_request = {40, 3};
        req_control_mode.max_charge_power = {30, 3};
        req_control_mode.min_charge_power = {27, 2};
        req_control_mode.max_charge_current = {400, 0};
        req_control_mode.max_voltage = {950, 0};
        req_control_mode.min_voltage = {150, 0};

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 30.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 330.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);

            REQUIRE(std::holds_alternative<Dynamic_DC_Res>(res.control_mode));
            const auto& res_control_mode = std::get<Dynamic_DC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_power) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.min_charge_power) == 10.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_current) == 250.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_voltage) == 900.0f);

            REQUIRE(res_control_mode.departure_time.value_or(0) >= 59);
            REQUIRE(res_control_mode.target_soc.value_or(0) == 95);
            REQUIRE(res_control_mode.ack_max_delay.value_or(0) == 30);
        }
    }

    GIVEN("Good case - MCS_BPT dynamic mode, mobility_needs_mode = 2") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::MCS_BPT, dt::DcConnector::Extended, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedBySecc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing);

        ctx.session = d20::Session(service_parameters);

        // Set control event for present voltage/current
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{330.0f, 30.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        // Set dynamic mode parameters
        state_helper.set_active_control_event(
            d20::UpdateDynamicModeParameters{std::time(nullptr) + 40, std::nullopt, 95});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<Dynamic_BPT_DC_Req>();
        req_control_mode.target_energy_request = {68, 3};
        req_control_mode.max_energy_request = {70, 3};
        req_control_mode.min_energy_request = {40, 3};
        req_control_mode.max_charge_power = {30, 3};
        req_control_mode.min_charge_power = {27, 2};
        req_control_mode.max_charge_current = {400, 0};
        req_control_mode.max_voltage = {950, 0};
        req_control_mode.min_voltage = {150, 0};
        req_control_mode.max_discharge_power = {11, 3};
        req_control_mode.min_discharge_power = {10, 0};
        req_control_mode.max_discharge_current = {30, 0};

        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(dt::from_RationalNumber(res.present_current) == 30.0f);
            REQUIRE(dt::from_RationalNumber(res.present_voltage) == 330.0f);
            REQUIRE(res.current_limit_achieved == false);
            REQUIRE(res.power_limit_achieved == false);
            REQUIRE(res.voltage_limit_achieved == false);

            REQUIRE(std::holds_alternative<Dynamic_BPT_DC_Res>(res.control_mode));
            const auto& res_control_mode = std::get<Dynamic_BPT_DC_Res>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_power) == 22000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.min_charge_power) == 10.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_charge_current) == 250.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_voltage) == 900.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_discharge_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.min_discharge_power) == 10.0f);
            REQUIRE(dt::from_RationalNumber(res_control_mode.max_discharge_current) == 30.0f);

            REQUIRE(res_control_mode.departure_time.value_or(0) >= 39);
            REQUIRE(res_control_mode.minimum_soc.value_or(0) == 95);
            REQUIRE(res_control_mode.ack_max_delay.value_or(0) == 30);
        }
    }

    GIVEN("Event then other V2GTP_MESSAGE") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};
        const auto result = fsm.feed(d20::Event::FAILED);
        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);
        }
    }

    GIVEN("SessionStopReq") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        ctx.session_config.cert_install_service = false;
        ctx.session_config.authorization_services = {dt::Authorization::EIM};

        message_20::SessionStopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.charging_session = dt::ChargingSession::Terminate;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_stop_res = response_message.value();
            REQUIRE(session_stop_res.response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("Sequence Error") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};

        ctx.session_config.cert_install_service = false;
        ctx.session_config.authorization_services = {dt::Authorization::EIM};

        message_20::SessionSetupRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.evccid = "WMIV1234567890ABCDEX";

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::SessionSetupResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_setup_res = response_message.value();
            REQUIRE(session_setup_res.response_code == dt::ResponseCode::FAILED_SequenceError);
        }
    }
}
