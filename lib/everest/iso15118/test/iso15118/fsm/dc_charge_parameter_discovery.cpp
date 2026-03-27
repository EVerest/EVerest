// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/d20/state/schedule_exchange.hpp>

#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

using DC_ModeReq = iso15118::message_20::datatypes::DC_CPDReqEnergyTransferMode;
using BPT_DC_ModeReq = iso15118::message_20::datatypes::BPT_DC_CPDReqEnergyTransferMode;

using DC_ModeRes = iso15118::message_20::datatypes::DC_CPDResEnergyTransferMode;
using BPT_DC_ModeRes = iso15118::message_20::datatypes::BPT_DC_CPDResEnergyTransferMode;

SCENARIO("ISO15118-20 dc charge parameter discovery state transitions") {

    const d20::EvseSetupConfig evse_setup = create_default_evse_setup();

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};

    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();
    ctx.session = d20::Session();

    GIVEN("Bad Case - Unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeParameterDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::DC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::DC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);

            REQUIRE(std::holds_alternative<DC_ModeRes>(res.transfer_mode));
            const auto& transfer_mode = std::get<DC_ModeRes>(res.transfer_mode);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_current) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_current) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_voltage) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_voltage) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_voltage) == 0);
            REQUIRE(transfer_mode.power_ramp_limit.has_value() == false);
        }
    }

    GIVEN("Bad Case: e.g. dc transfer mod instead of dc_bpt transfer mod - FAILED_WrongChargeParameter") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeParameterDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC_BPT};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::DC_BPT, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing);

        ctx.session = d20::Session(service_parameters);

        message_20::DC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_out = req.transfer_mode.emplace<DC_ModeReq>();
        req_out.max_charge_power = {50, 3};
        req_out.min_charge_power = {0, 0};
        req_out.max_charge_current = {125, 0};
        req_out.min_charge_current = {0, 0};
        req_out.max_voltage = {400, 0};
        req_out.min_voltage = {0, 0};

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::DC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_WrongChargeParameter);

            REQUIRE(std::holds_alternative<DC_ModeRes>(res.transfer_mode));
            const auto& transfer_mode = std::get<DC_ModeRes>(res.transfer_mode);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_current) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_current) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_voltage) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_voltage) == 0);
            REQUIRE(transfer_mode.power_ramp_limit.has_value() == false);
        }
    }

    GIVEN("Bad Case: e.g. DC_BPT transfer mod instead of dc transfer mod - FAILED_WrongChargeParameter") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeParameterDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        message_20::DC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_out = req.transfer_mode.emplace<BPT_DC_ModeReq>();
        req_out.max_charge_power = {50, 3};
        req_out.min_charge_power = {0, 0};
        req_out.max_charge_current = {125, 0};
        req_out.min_charge_current = {0, 0};
        req_out.max_voltage = {400, 0};
        req_out.min_voltage = {0, 0};
        req_out.max_discharge_power = {11, 3};
        req_out.min_discharge_power = {0, 0};
        req_out.max_discharge_current = {25, 0};
        req_out.min_discharge_current = {0, 0};

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::DC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_WrongChargeParameter);

            REQUIRE(std::holds_alternative<DC_ModeRes>(res.transfer_mode));
            const auto& transfer_mode = std::get<DC_ModeRes>(res.transfer_mode);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_current) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_current) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_voltage) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_voltage) == 0);
            REQUIRE(transfer_mode.power_ramp_limit.has_value() == false);
        }
    }

    GIVEN("Good Case: DC") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeParameterDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        // Configure power supply limits
        ctx.session_config.powersupply_limits.charge_limits.power.max = {22, 3};
        ctx.session_config.powersupply_limits.charge_limits.current.max = {25, 0};
        ctx.session_config.powersupply_limits.voltage.max = {900, 0};
        dt::RationalNumber power_ramp_limit = {20, 0};
        ctx.session_config.powersupply_limits.power_ramp_limit.emplace<>(power_ramp_limit);

        message_20::DC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_out = req.transfer_mode.emplace<DC_ModeReq>();
        req_out.max_charge_power = {50, 3};
        req_out.min_charge_power = {0, 0};
        req_out.max_charge_current = {125, 0};
        req_out.min_charge_current = {0, 0};
        req_out.max_voltage = {400, 0};
        req_out.min_voltage = {0, 0};

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ScheduleExchange);

            const auto response_message = ctx.get_response<message_20::DC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);

            REQUIRE(std::holds_alternative<DC_ModeRes>(res.transfer_mode));
            const auto& transfer_mode = std::get<DC_ModeRes>(res.transfer_mode);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 22000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_current) == 25);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_current) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_voltage) == 900);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_voltage) == 0);
            REQUIRE(transfer_mode.power_ramp_limit.has_value() == true);
            REQUIRE(dt::from_RationalNumber(transfer_mode.power_ramp_limit.value()) == 20);
        }
    }

    GIVEN("Good Case: DC_BPT") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeParameterDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC_BPT};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::DC_BPT, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing);

        ctx.session = d20::Session(service_parameters);

        // Configure power supply limits with discharge limits
        ctx.session_config.powersupply_limits.charge_limits.power.max = {22, 3};
        ctx.session_config.powersupply_limits.charge_limits.current.max = {25, 0};
        ctx.session_config.powersupply_limits.voltage.max = {900, 0};

        auto& discharge_limits = ctx.session_config.powersupply_limits.discharge_limits.emplace();
        discharge_limits.power.max = {11, 3};
        discharge_limits.current.max = {25, 0};

        message_20::DC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_out = req.transfer_mode.emplace<BPT_DC_ModeReq>();
        req_out.max_charge_power = {50, 3};
        req_out.min_charge_power = {0, 0};
        req_out.max_charge_current = {125, 0};
        req_out.min_charge_current = {0, 0};
        req_out.max_voltage = {400, 0};
        req_out.min_voltage = {0, 0};
        req_out.max_discharge_power = {11, 3};
        req_out.min_discharge_power = {0, 0};
        req_out.max_discharge_current = {25, 0};
        req_out.min_discharge_current = {0, 0};

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ScheduleExchange);

            const auto response_message = ctx.get_response<message_20::DC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);

            REQUIRE(std::holds_alternative<BPT_DC_ModeRes>(res.transfer_mode));
            const auto& transfer_mode = std::get<BPT_DC_ModeRes>(res.transfer_mode);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 22000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_current) == 25);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_current) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_voltage) == 900);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_voltage) == 0);
            REQUIRE(transfer_mode.power_ramp_limit.has_value() == false);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_discharge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_discharge_power) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_discharge_current) == 25);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_discharge_current) == 0);
        }
    }

    GIVEN("Bad Case: Provided DC charge limits but the ev wants bpt charge parameter - FAILED") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeParameterDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC_BPT};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::DC_BPT, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing);

        ctx.session = d20::Session(service_parameters);

        // Configure power supply limits with only charge limits (no discharge limits)
        ctx.session_config.powersupply_limits.charge_limits.power.max = {22, 3};
        ctx.session_config.powersupply_limits.charge_limits.current.max = {25, 0};
        ctx.session_config.powersupply_limits.voltage.max = {900, 0};
        // Note: discharge_limits is not set, which should cause FAILED response

        message_20::DC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_out = req.transfer_mode.emplace<BPT_DC_ModeReq>();
        req_out.max_charge_power = {50, 3};
        req_out.min_charge_power = {0, 0};
        req_out.max_charge_current = {125, 0};
        req_out.min_charge_current = {0, 0};
        req_out.max_voltage = {400, 0};
        req_out.min_voltage = {0, 0};
        req_out.max_discharge_power = {11, 3};
        req_out.min_discharge_power = {0, 0};
        req_out.max_discharge_current = {25, 0};
        req_out.min_discharge_current = {0, 0};

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::DC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);

            REQUIRE(std::holds_alternative<DC_ModeRes>(res.transfer_mode));
            const auto& transfer_mode = std::get<DC_ModeRes>(res.transfer_mode);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_current) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_current) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_voltage) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_voltage) == 0);
            REQUIRE(transfer_mode.power_ramp_limit.has_value() == false);
        }
    }

    GIVEN("Bad Case: EV Parameter does not fit in evse parameters - FAILED_WrongChargeParameter") {
        // todo(sl): Missing test
    }

    GIVEN("Good Case: MCS") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeParameterDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::MCS};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::MCS, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);

        // Configure power supply limits
        ctx.session_config.powersupply_limits.charge_limits.power.max = {22, 3};
        ctx.session_config.powersupply_limits.charge_limits.current.max = {25, 0};
        ctx.session_config.powersupply_limits.voltage.max = {900, 0};
        dt::RationalNumber power_ramp_limit = {20, 0};
        ctx.session_config.powersupply_limits.power_ramp_limit.emplace<>(power_ramp_limit);

        message_20::DC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_out = req.transfer_mode.emplace<DC_ModeReq>();
        req_out.max_charge_power = {50, 3};
        req_out.min_charge_power = {0, 0};
        req_out.max_charge_current = {125, 0};
        req_out.min_charge_current = {0, 0};
        req_out.max_voltage = {400, 0};
        req_out.min_voltage = {0, 0};

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ScheduleExchange);

            const auto response_message = ctx.get_response<message_20::DC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);

            REQUIRE(std::holds_alternative<DC_ModeRes>(res.transfer_mode));
            const auto& transfer_mode = std::get<DC_ModeRes>(res.transfer_mode);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 22000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_current) == 25);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_current) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_voltage) == 900);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_voltage) == 0);
            REQUIRE(transfer_mode.power_ramp_limit.has_value() == true);
            REQUIRE(dt::from_RationalNumber(transfer_mode.power_ramp_limit.value()) == 20);
        }
    }

    GIVEN("Good Case: MCS_BPT") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeParameterDiscovery>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::MCS_BPT};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::MCS_BPT, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, dt::BptChannel::Unified,
            dt::GeneratorMode::GridFollowing);

        ctx.session = d20::Session(service_parameters);

        // Configure power supply limits with discharge limits
        ctx.session_config.powersupply_limits.charge_limits.power.max = {22, 3};
        ctx.session_config.powersupply_limits.charge_limits.current.max = {25, 0};
        ctx.session_config.powersupply_limits.voltage.max = {900, 0};

        auto& discharge_limits = ctx.session_config.powersupply_limits.discharge_limits.emplace();
        discharge_limits.power.max = {11, 3};
        discharge_limits.current.max = {25, 0};

        message_20::DC_ChargeParameterDiscoveryRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        auto& req_out = req.transfer_mode.emplace<BPT_DC_ModeReq>();
        req_out.max_charge_power = {50, 3};
        req_out.min_charge_power = {0, 0};
        req_out.max_charge_current = {125, 0};
        req_out.min_charge_current = {0, 0};
        req_out.max_voltage = {400, 0};
        req_out.min_voltage = {0, 0};
        req_out.max_discharge_power = {11, 3};
        req_out.min_discharge_power = {0, 0};
        req_out.max_discharge_current = {25, 0};
        req_out.min_discharge_current = {0, 0};

        state_helper.handle_request(req);

        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::ScheduleExchange);

            const auto response_message = ctx.get_response<message_20::DC_ChargeParameterDiscoveryResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);

            REQUIRE(std::holds_alternative<BPT_DC_ModeRes>(res.transfer_mode));
            const auto& transfer_mode = std::get<BPT_DC_ModeRes>(res.transfer_mode);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_power) == 22000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_power) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_charge_current) == 25);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_charge_current) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_voltage) == 900);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_voltage) == 0);
            REQUIRE(transfer_mode.power_ramp_limit.has_value() == false);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_discharge_power) == 11000);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_discharge_power) == 0);
            REQUIRE(dt::from_RationalNumber(transfer_mode.max_discharge_current) == 25);
            REQUIRE(dt::from_RationalNumber(transfer_mode.min_discharge_current) == 0);
        }
    }

    GIVEN("Event then other V2GTP_MESSAGE") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeParameterDiscovery>()};
        const auto result = fsm.feed(d20::Event::FAILED);
        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);
        }
    }

    GIVEN("SessionStopReq") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeParameterDiscovery>()};

        // Setting up session_config based on test
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
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_stop_res = response_message.value();
            REQUIRE(session_stop_res.response_code == dt::ResponseCode::OK);
        }
    }
    GIVEN("Sequence Error") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeParameterDiscovery>()};

        // Setting up session_config based on test
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
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_ChargeParameterDiscovery);

            const auto response_message = ctx.get_response<message_20::SessionSetupResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_setup_res = response_message.value();
            REQUIRE(session_setup_res.response_code == dt::ResponseCode::FAILED_SequenceError);
        }
    }
}
