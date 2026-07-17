// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/ac_der_iec_charge_loop.hpp>
#include <iso15118/d20/state/power_delivery.hpp>
#include <iso15118/d20/state/session_stop.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/message/ac_der_iec_charge_loop.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

SCENARIO("ISO15118-20 der iec ac charge loop state transitions") {

    const auto evse_id = std::string("everest se");
    const std::vector<dt::ServiceCategory> supported_energy_services = {
        dt::ServiceCategory::AC_DER_IEC,
    };
    const auto cert_install = false;
    const std::vector<dt::Authorization> auth_services = {dt::Authorization::EIM};
    const std::vector<uint16_t> vas_services{};

    d20::DcTransferLimits dc_limits;

    d20::AcTransferLimits ac_limits;
    ac_limits.charge_power.max = dt::from_float(11000);
    ac_limits.charge_power.min = dt::from_float(2000);
    ac_limits.nominal_frequency = dt::from_float(50);
    ac_limits.power_ramp_limitation = dt::from_float(2);

    d20::IecDerTransferLimits der_iec_limits;
    der_iec_limits.max_discharge_power = dt::from_float(11000);
    der_iec_limits.nominal_charge_power = dt::from_float(11000);
    der_iec_limits.nominal_discharge_power = dt::from_float(11000);

    d20::DcTransferLimits powersupply_limits;

    d20::DerSetupConfig der_setup_config;
    der_setup_config.grid_connection_mode = iec::GridConnectionMode::GridConnected;
    der_setup_config.operating_mode = iec::OperatingMode::GridFollowing;

    const auto no_der_function_selected = std::bitset<12>{};

    const std::vector<d20::ControlMobilityNeedsModes> control_mobility_modes = {
        {dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc}};

    const session::EvseSetupConfig evse_setup{evse_id,
                                          supported_energy_services,
                                          auth_services,
                                          vas_services,
                                          cert_install,
                                          dc_limits,
                                          ac_limits,
                                          der_iec_limits,
                                          control_mobility_modes,
                                          std::nullopt,
                                          std::nullopt,
                                          std::nullopt,
                                          der_setup_config,
                                          powersupply_limits};

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(session::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();

    GIVEN("Bad case - Unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_IEC_ChargeLoop>()};

        ctx.session_config.supported_energy_transfer_services = supported_energy_services;
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::DER_AC_ChargeLoopRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<dt::DER_Scheduled_AC_CLReqControlMode>();
        req_control_mode.present_active_power = {0, 0};

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_IEC_ChargeLoop);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::DER_AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE_FALSE(res.target_frequency.has_value());
            REQUIRE(std::holds_alternative<dt::DER_Scheduled_AC_CLResControlMode>(res.control_mode));
            const auto& control_mode = std::get<dt::DER_Scheduled_AC_CLResControlMode>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_power) == 0);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 0);
            // TODO(SL): Add checks for all optional parameters
        }
    }

    GIVEN("Bad case - false control mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_IEC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC_DER_IEC, dt::AcConnector::ThreePhase, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230, no_der_function_selected);

        ctx.session = d20::Session(service_parameters);

        message_20::DER_AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<dt::DER_Scheduled_AC_CLReqControlMode>();
        req_control_mode.present_active_power = {0, 0};
        req_control_mode.max_discharge_power = {0, 0};
        req_control_mode.min_discharge_power = {0, 0};
        req_control_mode.grid_event_condition = 0;

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_IEC_ChargeLoop);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::DER_AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE_FALSE(res.target_frequency.has_value());
            REQUIRE(std::holds_alternative<dt::DER_Scheduled_AC_CLResControlMode>(res.control_mode));
            const auto& control_mode = std::get<dt::DER_Scheduled_AC_CLResControlMode>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_power) == 0);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 0);
            // TODO(SL): Add checks for all optional parameters
        }
    }

    GIVEN("Good case - AC scheduled mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_IEC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230, no_der_function_selected);

        ctx.session = d20::Session(service_parameters);

        // Set control events for target and present power
        d20::AcTargetPower target_power{};
        state_helper.set_active_control_event(target_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        d20::AcPresentPower present_power{};
        present_power.present_active_power = {11, 3};
        state_helper.set_active_control_event(present_power);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DER_AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<dt::DER_Scheduled_AC_CLReqControlMode>();
        req_control_mode.present_active_power = {11, 3};
        req_control_mode.max_discharge_power = {11, 3};
        req_control_mode.min_discharge_power = {2, 3};
        req_control_mode.grid_event_condition = 0;

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_IEC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DER_AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE_FALSE(res.target_frequency.has_value());
            REQUIRE(std::holds_alternative<dt::DER_Scheduled_AC_CLResControlMode>(res.control_mode));

            const auto& control_mode = std::get<dt::DER_Scheduled_AC_CLResControlMode>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(control_mode.present_active_power.value_or(dt::RationalNumber{0, 0})) ==
                    11000.0f);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 11000);
            REQUIRE_FALSE(control_mode.dso_cos_phi_setpoint.has_value());
            REQUIRE_FALSE(control_mode.dso_q_setpoint.has_value());
        }
    }

    GIVEN("Good case - AC dynamic mode") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_IEC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230, no_der_function_selected);

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

        message_20::DER_AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<dt::DER_Dynamic_AC_CLReqControlMode>();
        req_control_mode.present_active_power = {11, 3};
        req_control_mode.max_charge_power = {11, 3};
        req_control_mode.min_charge_power = {4, 0};
        req_control_mode.present_reactive_power = {10, 0};
        req_control_mode.max_discharge_power = {11, 3};
        req_control_mode.min_discharge_power = {2, 3};
        req_control_mode.grid_event_condition = 0;

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_IEC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DER_AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE_FALSE(res.target_frequency.has_value());
            REQUIRE(std::holds_alternative<dt::DER_Dynamic_AC_CLResControlMode>(res.control_mode));

            const auto& control_mode = std::get<dt::DER_Dynamic_AC_CLResControlMode>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(control_mode.present_active_power.value_or(dt::RationalNumber{0, 0})) ==
                    11000.0f);
            REQUIRE(dt::from_RationalNumber(control_mode.target_active_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 11000);
            REQUIRE_FALSE(control_mode.dso_cos_phi_setpoint.has_value());
            REQUIRE_FALSE(control_mode.dso_q_setpoint.has_value());
        }
    }

    GIVEN("Good case - AC dynamic mode, mobility_needs_mode = 2") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_IEC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedBySecc, dt::Pricing::NoPricing, 230, no_der_function_selected);

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
        const d20::UpdateDynamicModeParameters dynamic_parameters = {std::time(nullptr) + 40, 95, 80};
        state_helper.set_active_control_event(dynamic_parameters);
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DER_AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<dt::DER_Dynamic_AC_CLReqControlMode>();
        req_control_mode.present_active_power = {11, 3};
        req_control_mode.max_charge_power = {11, 3};
        req_control_mode.min_charge_power = {4, 0};
        req_control_mode.present_reactive_power = {10, 0};
        req_control_mode.present_reactive_power = {10, 0};
        req_control_mode.max_discharge_power = {11, 3};
        req_control_mode.min_discharge_power = {2, 3};
        req_control_mode.grid_event_condition = 0;

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_IEC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DER_AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE_FALSE(res.target_frequency.has_value());
            REQUIRE(std::holds_alternative<dt::DER_Dynamic_AC_CLResControlMode>(res.control_mode));

            const auto& control_mode = std::get<dt::DER_Dynamic_AC_CLResControlMode>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(control_mode.present_active_power.value_or(dt::RationalNumber{0, 0})) ==
                    11000.0f);
            REQUIRE(dt::from_RationalNumber(control_mode.target_active_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 11000);
            REQUIRE_FALSE(control_mode.dso_cos_phi_setpoint.has_value());
            REQUIRE_FALSE(control_mode.dso_q_setpoint.has_value());

            REQUIRE(control_mode.departure_time.value_or(0) >= 39);
            REQUIRE(control_mode.target_soc.value_or(0) == 95);
            REQUIRE(control_mode.minimum_soc.value_or(0) == 80);
            REQUIRE(control_mode.ack_max_delay.value_or(0) == 30);
        }
    }

    GIVEN("Good case - Der control functions") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_IEC_ChargeLoop>()};

        ctx.session_config.der_setup_config.supported_der_control_functions.clear();

        ctx.session_config.der_setup_config
            .supported_der_control_functions[iec::DERControlName::DSOQSetpointProvision] =
            iec::DSOQSetpoint{50, std::nullopt, std::nullopt, false, 0};
        ctx.session_config.der_setup_config
            .supported_der_control_functions[iec::DERControlName::DSOCosPhiSetpointProvision] =
            iec::DSOCosPhiSetpoint{345, std::nullopt, std::nullopt, iec::PowerFactorExcitation::OverExcited, true, 422};

        auto der_functions = std::bitset<12>{};
        der_functions.set(static_cast<size_t>(iec::DERControlName::DSOQSetpointProvision), true);
        der_functions.set(static_cast<size_t>(iec::DERControlName::DSOCosPhiSetpointProvision), true);

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230, der_functions);

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

        message_20::DER_AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<dt::DER_Dynamic_AC_CLReqControlMode>();
        req_control_mode.present_active_power = {11, 3};
        req_control_mode.max_charge_power = {11, 3};
        req_control_mode.min_charge_power = {4, 0};
        req_control_mode.present_reactive_power = {10, 0};
        req_control_mode.max_discharge_power = {11, 3};
        req_control_mode.min_discharge_power = {2, 3};
        req_control_mode.grid_event_condition = 0;

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_IEC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DER_AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE_FALSE(res.target_frequency.has_value());
            REQUIRE(std::holds_alternative<dt::DER_Dynamic_AC_CLResControlMode>(res.control_mode));

            const auto& control_mode = std::get<dt::DER_Dynamic_AC_CLResControlMode>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(control_mode.present_active_power.value_or(dt::RationalNumber{0, 0})) ==
                    11000.0f);
            REQUIRE(dt::from_RationalNumber(control_mode.target_active_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 11000);
            REQUIRE(control_mode.dso_cos_phi_setpoint.has_value());
            const auto& cos_phi = control_mode.dso_cos_phi_setpoint.value();
            REQUIRE(dt::from_RationalNumber(cos_phi.dso_cos_phi_setpoint_value) == 345);
            REQUIRE_FALSE(cos_phi.dso_cos_phi_setpoint_value_L2);
            REQUIRE_FALSE(cos_phi.dso_cos_phi_setpoint_value_L3);
            REQUIRE(cos_phi.excitation == dt::PowerFactorExcitation::OverExcited);
            REQUIRE(cos_phi.pt1_response_reactive_power);
            REQUIRE(dt::from_RationalNumber(cos_phi.step_response_time_constant_reactive_power) == 422);

            REQUIRE(control_mode.dso_q_setpoint.has_value());
            const auto& setpoint = control_mode.dso_q_setpoint.value();
            REQUIRE(dt::from_RationalNumber(setpoint.dso_q_setpoint_value) == 50);
            REQUIRE_FALSE(setpoint.dso_q_setpoint_value_L2);
            REQUIRE_FALSE(setpoint.dso_q_setpoint_value_L3);
            REQUIRE_FALSE(setpoint.pt1_response_reactive_power);
            REQUIRE(dt::from_RationalNumber(setpoint.step_response_time_constant_reactive_power) == 0);
        }
    }

    GIVEN("Good case - AC dynamic mode & pause from charger") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_IEC_ChargeLoop>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230, no_der_function_selected);

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

        message_20::DER_AC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_control_mode = req.control_mode.emplace<dt::DER_Dynamic_AC_CLReqControlMode>();
        req_control_mode.present_active_power = {11, 3};
        req_control_mode.max_charge_power = {11, 3};
        req_control_mode.min_charge_power = {4, 0};
        req_control_mode.present_reactive_power = {10, 0};
        req_control_mode.max_discharge_power = {11, 3};
        req_control_mode.min_discharge_power = {2, 3};
        req_control_mode.grid_event_condition = 0;

        req.meter_info_requested = false;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_IEC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::DER_AC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE_FALSE(res.target_frequency.has_value());
            REQUIRE(std::holds_alternative<dt::DER_Dynamic_AC_CLResControlMode>(res.control_mode));

            const auto& control_mode = std::get<dt::DER_Dynamic_AC_CLResControlMode>(res.control_mode);
            REQUIRE(dt::from_RationalNumber(control_mode.present_active_power.value_or(dt::RationalNumber{0, 0})) ==
                    11000.0f);
            REQUIRE(dt::from_RationalNumber(control_mode.target_active_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(control_mode.max_charge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(control_mode.max_discharge_power) == 11000);
            REQUIRE_FALSE(control_mode.dso_cos_phi_setpoint.has_value());
            REQUIRE_FALSE(control_mode.dso_q_setpoint.has_value());

            REQUIRE(res.status.has_value() == true);
            REQUIRE(res.status.value().notification == dt::EvseNotification::Pause);
            REQUIRE(res.status.value().notification_max_delay == 60);
        }
    }

    GIVEN("Sequence Error") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_IEC_ChargeLoop>()};

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
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_IEC_ChargeLoop);

            const auto response_message = ctx.get_response<message_20::SessionSetupResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_setup_res = response_message.value();
            REQUIRE(session_setup_res.response_code == dt::ResponseCode::FAILED_SequenceError);
        }
    }
}
