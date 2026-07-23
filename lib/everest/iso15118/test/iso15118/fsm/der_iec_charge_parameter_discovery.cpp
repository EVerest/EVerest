// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/d20/state/schedule_exchange.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

SCENARIO("ISO15118-20 der iec ac charge parameter discovery state transitions") {

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

    GIVEN("Bad Case - Unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_IEC_ChargeParameterDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = supported_energy_services;
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::DER_AC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;

        auto& req_out = req.transfer_mode;
        req_out.processing = dt::Processing::Ongoing;
        req_out.max_discharge_power = {11, 3};
        req_out.min_discharge_power = {3, 3};
        req_out.max_charge_power = {11, 3};
        req_out.min_charge_power = {23, 2};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_IEC_ChargeParameterDiscovery);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::DER_AC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);

            const auto& transfer_mode = res.transfer_mode;
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 0.0f);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 0.0f);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_frequency) == 0.0f);

            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_charge_power) == 0.0f);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_discharge_power) == 0.0f);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_discharge_power) == 0.0f);

            REQUIRE(transfer_mode.operating_mode == dt::OperatingMode::GridFollowing);
            REQUIRE(transfer_mode.grid_connection_mode == dt::GridConnectionMode::GridConnected);

            REQUIRE_FALSE(transfer_mode.der_control.over_voltage_fault_ride_through.has_value());
            REQUIRE_FALSE(transfer_mode.der_control.under_voltage_fault_ride_through.has_value());
            REQUIRE_FALSE(transfer_mode.der_control.zero_current.has_value());
            REQUIRE_FALSE(transfer_mode.der_control.reactive_power_support.has_value());
            REQUIRE_FALSE(transfer_mode.der_control.active_power_support.has_value());
            REQUIRE_FALSE(transfer_mode.der_control.max_level_dc_injection.has_value());
        }
    }

    GIVEN("Good Case - No der functions selected (ongoing)") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_IEC_ChargeParameterDiscovery>()};

        std::bitset<12> der_control_functions{};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC_DER_IEC, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230, der_control_functions);

        ctx.session = d20::Session(service_parameters);

        message_20::DER_AC_ChargeParameterDiscoveryRequest req;

        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        auto& req_out = req.transfer_mode;
        req_out.processing = dt::Processing::Ongoing;
        req_out.max_discharge_power = {11, 3};
        req_out.min_discharge_power = {3, 3};
        req_out.max_charge_power = {11, 3};
        req_out.min_charge_power = {23, 2};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {

            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_IEC_ChargeParameterDiscovery);
            REQUIRE(ctx.session_stopped == false);

            const auto response_message = ctx.get_response<message_20::DER_AC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);

            const auto& transfer_mode = res.transfer_mode;
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 2000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_frequency) == 50);
            REQUIRE(dt::from_RationalNumber(transfer_mode.power_ramp_limitation.value_or(dt::RationalNumber{0, 0})) ==
                    2);

            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_charge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_discharge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_discharge_power) == 11000);

            REQUIRE(transfer_mode.operating_mode == dt::OperatingMode::GridFollowing);
            REQUIRE(transfer_mode.grid_connection_mode == dt::GridConnectionMode::GridConnected);

            REQUIRE_FALSE(transfer_mode.der_control.over_voltage_fault_ride_through.has_value());
            REQUIRE_FALSE(transfer_mode.der_control.under_voltage_fault_ride_through.has_value());
            REQUIRE_FALSE(transfer_mode.der_control.zero_current.has_value());
            REQUIRE_FALSE(transfer_mode.der_control.reactive_power_support.has_value());
            REQUIRE_FALSE(transfer_mode.der_control.active_power_support.has_value());
            REQUIRE_FALSE(transfer_mode.der_control.max_level_dc_injection.has_value());
        }
    }

    GIVEN("Good Case - Some DER functions selected (finished)") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_IEC_ChargeParameterDiscovery>()};

        ctx.session_config.der_setup_config.supported_der_control_functions.clear();

        ctx.session_config.der_setup_config.supported_der_control_functions[iec::DERControlName::ZeroCurrentMode] =
            iec::ZeroCurrent{std::nullopt, std::nullopt, std::nullopt, std::nullopt, true, 200, false, 0};
        ctx.session_config.der_setup_config
            .supported_der_control_functions[iec::DERControlName::DCInjectionRestriction] =
            iec::MaximumLevelDCInjection{450};

        std::bitset<12> der_control_functions{};
        der_control_functions.set(static_cast<size_t>(iec::DERControlName::ZeroCurrentMode), true);
        der_control_functions.set(static_cast<size_t>(iec::DERControlName::DCInjectionRestriction), true);

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC_DER_IEC, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230, der_control_functions);

        ctx.session = d20::Session(service_parameters);

        message_20::DER_AC_ChargeParameterDiscoveryRequest req;

        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        auto& req_out = req.transfer_mode;
        req_out.processing = dt::Processing::Finished;
        req_out.max_discharge_power = {11, 3};
        req_out.min_discharge_power = {3, 3};
        req_out.max_charge_power = {11, 3};
        req_out.min_charge_power = {23, 2};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {

            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ScheduleExchange);
            REQUIRE(ctx.session_stopped == false);

            const auto response_message = ctx.get_response<message_20::DER_AC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);

            const auto& transfer_mode = res.transfer_mode;
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 2000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_frequency) == 50);
            REQUIRE(dt::from_RationalNumber(transfer_mode.power_ramp_limitation.value_or(dt::RationalNumber{0, 0})) ==
                    2);

            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_charge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_discharge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_discharge_power) == 11000);

            REQUIRE(transfer_mode.operating_mode == dt::OperatingMode::GridFollowing);
            REQUIRE(transfer_mode.grid_connection_mode == dt::GridConnectionMode::GridConnected);

            REQUIRE_FALSE(transfer_mode.der_control.over_voltage_fault_ride_through.has_value());
            REQUIRE_FALSE(transfer_mode.der_control.under_voltage_fault_ride_through.has_value());
            REQUIRE(transfer_mode.der_control.zero_current.has_value());
            const auto& res_zero_current = transfer_mode.der_control.zero_current.value();
            REQUIRE_FALSE(res_zero_current.over_voltage_limit.has_value());
            REQUIRE_FALSE(res_zero_current.under_voltage_limit.has_value());
            REQUIRE_FALSE(res_zero_current.over_voltage_recovery_limit.has_value());
            REQUIRE_FALSE(res_zero_current.under_voltage_recovery_limit.has_value());
            REQUIRE(res_zero_current.pt1_response_active_power);
            REQUIRE(dt::from_RationalNumber(res_zero_current.step_response_time_constant_active_power) == 200);
            REQUIRE_FALSE(res_zero_current.pt1_response_reactive_power);
            REQUIRE(dt::from_RationalNumber(res_zero_current.step_response_time_constant_reactive_power) == 0);

            REQUIRE_FALSE(transfer_mode.der_control.reactive_power_support.has_value());
            REQUIRE_FALSE(transfer_mode.der_control.active_power_support.has_value());
            REQUIRE(dt::from_RationalNumber(
                        transfer_mode.der_control.max_level_dc_injection.value_or(dt::RationalNumber{0, 0})) == 450);
        }
    }

    // TODO(SL): Adding more unit tests with every der function

    GIVEN("SessionStopReq") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_IEC_ChargeParameterDiscovery>()};

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
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_IEC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_stop_res = response_message.value();
            REQUIRE(session_stop_res.response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("Sequence Error") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_DER_IEC_ChargeParameterDiscovery>()};

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
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_DER_IEC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::SessionSetupResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_setup_res = response_message.value();
            REQUIRE(session_setup_res.response_code == dt::ResponseCode::FAILED_SequenceError);
        }
    }
}
