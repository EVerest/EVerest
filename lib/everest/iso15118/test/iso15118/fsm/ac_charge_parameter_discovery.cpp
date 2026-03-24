// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/ac_charge_parameter_discovery.hpp>
#include <iso15118/d20/state/schedule_exchange.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

using AC_ModeReq = dt::AC_CPDReqEnergyTransferMode;
using BPT_AC_ModeReq = dt::BPT_AC_CPDReqEnergyTransferMode;

using AC_ModeRes = dt::AC_CPDResEnergyTransferMode;
using BPT_AC_ModeRes = dt::BPT_AC_CPDResEnergyTransferMode;

SCENARIO("ISO15118-20 ac charge parameter discovery state transitions") {

    const auto evse_id = std::string("everest se");
    const std::vector<dt::ServiceCategory> supported_energy_services = {dt::ServiceCategory::AC,
                                                                        dt::ServiceCategory::AC_BPT};
    const auto cert_install = false;
    const std::vector<dt::Authorization> auth_services = {dt::Authorization::EIM};
    const std::vector<uint16_t> vas_services{};

    d20::DcTransferLimits dc_limits;
    d20::AcTransferLimits ac_limits;
    d20::DcTransferLimits powersupply_limits;

    const std::vector<d20::ControlMobilityNeedsModes> control_mobility_modes = {
        {dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc}};

    const d20::EvseSetupConfig evse_setup{
        evse_id,   supported_energy_services, auth_services, vas_services, cert_install, dc_limits,
        ac_limits, control_mobility_modes,    std::nullopt,  std::nullopt, std::nullopt, powersupply_limits};

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();

    GIVEN("Bad Case - Unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeParameterDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = supported_energy_services;
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::AC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;

        auto& req_out = req.transfer_mode.emplace<AC_ModeReq>();
        req_out.max_charge_power = {11, 3};
        req_out.min_charge_power = {23, 2};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeParameterDiscovery);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::AC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);

            REQUIRE(std::holds_alternative<AC_ModeRes>(res.transfer_mode));
            const auto& transfer_mode = std::get<AC_ModeRes>(res.transfer_mode);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 0.0f);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 0.0f);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_frequency) == 0.0f);
        }
    }

    GIVEN("Bad Case: e.g. ac transfer mod instead of ac_bpt transfer mod - FAILED_WrongChargeParameter") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeParameterDiscovery>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC_BPT, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing, 230, dt::GridCodeIslandingDetectionMethod::Passive);

        ctx.session = d20::Session(service_parameters);

        message_20::AC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_out = req.transfer_mode.emplace<AC_ModeReq>();
        req_out.max_charge_power = {11, 3};
        req_out.min_charge_power = {23, 2};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeParameterDiscovery);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::AC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_WrongChargeParameter);

            REQUIRE(std::holds_alternative<AC_ModeRes>(res.transfer_mode));
            const auto& transfer_mode = std::get<AC_ModeRes>(res.transfer_mode);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 0.0f);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 0.0f);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_frequency) == 0.0f);
        }
    }

    GIVEN("Bad Case: e.g. AC_BPT transfer mod instead of ac transfer mod - FAILED_WrongChargeParameter") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeParameterDiscovery>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230);

        ctx.session = d20::Session(service_parameters);

        message_20::AC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_out = req.transfer_mode.emplace<BPT_AC_ModeReq>();
        req_out.max_charge_power = {11, 3};
        req_out.min_charge_power = {23, 2};
        req_out.max_discharge_power = {11, 3};
        req_out.min_discharge_power = {23, 2};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::AC_ChargeParameterDiscovery);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::AC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_WrongChargeParameter);

            REQUIRE(std::holds_alternative<AC_ModeRes>(res.transfer_mode));
            const auto& transfer_mode = std::get<AC_ModeRes>(res.transfer_mode);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 0.0f);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 0.0f);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_frequency) == 0.0f);
        }
    }

    GIVEN("Good Case: AC") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeParameterDiscovery>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230);

        ctx.session = d20::Session(service_parameters);

        float max_charge_power = 11000.0f;
        float min_charge_power = 11000.0f;
        float nominal_frequency = 50.0f;
        float power_ramp_limitation = 2.0f;

        ctx.session_config.ac_limits.charge_power.max = dt::from_float(max_charge_power);
        ctx.session_config.ac_limits.charge_power.min = dt::from_float(min_charge_power);
        ctx.session_config.ac_limits.nominal_frequency = dt::from_float(nominal_frequency);
        ctx.session_config.ac_limits.power_ramp_limitation = dt::from_float(power_ramp_limitation);

        message_20::AC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_out = req.transfer_mode.emplace<AC_ModeReq>();
        req_out.max_charge_power = {11, 3};
        req_out.min_charge_power = {23, 2};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ScheduleExchange);

            const auto response_message = ctx.get_response<message_20::AC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);

            REQUIRE(std::holds_alternative<AC_ModeRes>(res.transfer_mode));
            const auto& transfer_mode = std::get<AC_ModeRes>(res.transfer_mode);

            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == max_charge_power);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == min_charge_power);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_frequency) == nominal_frequency);
            REQUIRE(transfer_mode.power_ramp_limitation.has_value() == true);
            REQUIRE(dt::from_RationalNumber(transfer_mode.power_ramp_limitation.value()) == power_ramp_limitation);
        }
    }

    GIVEN("Good Case: AC_BPT") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::AC_ChargeParameterDiscovery>()};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::AC_BPT, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing, 230, dt::GridCodeIslandingDetectionMethod::Passive);

        ctx.session = d20::Session(service_parameters);

        float max_charge_power = 11000.0f;
        float min_charge_power = 11000.0f;
        float nominal_frequency = 50.0f;
        float power_ramp_limitation = 2.0f;
        float max_discharge_power = 6000.0f;
        float min_discharge_power = 3000.0f;

        ctx.session_config.ac_limits.charge_power.max = dt::from_float(max_charge_power);
        ctx.session_config.ac_limits.charge_power.min = dt::from_float(min_charge_power);
        ctx.session_config.ac_limits.nominal_frequency = dt::from_float(nominal_frequency);
        ctx.session_config.ac_limits.power_ramp_limitation = dt::from_float(power_ramp_limitation);

        d20::Limit<dt::RationalNumber> discharge_power;
        discharge_power.max = dt::from_float(max_discharge_power);
        discharge_power.min = dt::from_float(min_discharge_power);
        ctx.session_config.ac_limits.discharge_power = discharge_power;

        message_20::AC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_out = req.transfer_mode.emplace<BPT_AC_ModeReq>();
        req_out.max_charge_power = {11, 3};
        req_out.min_charge_power = {23, 2};
        req_out.max_discharge_power = {11, 3};
        req_out.min_discharge_power = {23, 2};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ScheduleExchange);

            const auto response_message = ctx.get_response<message_20::AC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);

            REQUIRE(std::holds_alternative<BPT_AC_ModeRes>(res.transfer_mode));
            const auto& transfer_mode = std::get<BPT_AC_ModeRes>(res.transfer_mode);

            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == max_charge_power);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == min_charge_power);
            REQUIRE(dt::from_RationalNumber(transfer_mode.nominal_frequency) == nominal_frequency);
            REQUIRE(transfer_mode.power_ramp_limitation.has_value() == true);
            REQUIRE(dt::from_RationalNumber(transfer_mode.power_ramp_limitation.value()) == power_ramp_limitation);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_discharge_power) == max_discharge_power);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_discharge_power) == min_discharge_power);
        }
    }

    // GIVEN("Bad Case: EV Parameter does not fit in evse parameters - FAILED_WrongChargeParameter") {
    // } // TODO(SL)
}
