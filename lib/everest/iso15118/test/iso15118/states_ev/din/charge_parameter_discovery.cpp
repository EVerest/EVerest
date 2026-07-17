// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/ev/state/charge_parameter_discovery.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;
using namespace din::ev::state::charge_parameter_discovery;

SCENARIO("EVCC DIN ChargeParameterDiscovery request/response handling") {
    GIVEN("A DC request") {
        dt::DcEvChargeParameter dc;
        dc.dc_ev_status.ev_ready = false;
        dc.ev_maximum_current_limit = 300.0;
        dc.ev_maximum_voltage_limit = 900.0;
        const auto req = create_request(dt::EnergyTransferMode::DC_extended, dc);
        THEN("The requested energy transfer type and DC parameters are set") {
            REQUIRE(req.ev_requested_energy_transfer_type == dt::EnergyTransferMode::DC_extended);
            REQUIRE(req.dc_ev_charge_parameter.has_value());
            REQUIRE(req.dc_ev_charge_parameter->ev_maximum_current_limit == 300.0);
            REQUIRE(req.dc_ev_charge_parameter->ev_maximum_voltage_limit == 900.0);
        }
    }

    GIVEN("An Ongoing response") {
        message_din::ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Ongoing;
        THEN("The action is Retry") {
            REQUIRE(handle_response(res).action == Action::Retry);
        }
    }

    GIVEN("A Finished response with EVSE limits (explicit power)") {
        message_din::ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;
        dt::DcEvseChargeParameter param;
        param.evse_maximum_current_limit = 400.0;
        param.evse_maximum_voltage_limit = 920.0;
        param.evse_maximum_power_limit = 360000.0;
        res.dc_evse_charge_parameter = param;
        const auto result = handle_response(res);
        THEN("Done, and the limits are extracted") {
            REQUIRE(result.action == Action::Done);
            REQUIRE(result.limits.has_value());
            REQUIRE(result.limits->current == 400.0f);
            REQUIRE(result.limits->voltage == 920.0f);
            REQUIRE(result.limits->power == 360000.0f);
        }
    }

    GIVEN("A Finished response with EVSENotification StopCharging") {
        message_din::ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;
        dt::DcEvseChargeParameter param;
        param.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        param.dc_evse_status.evse_notification = dt::EvseNotification::StopCharging;
        param.evse_maximum_current_limit = 400.0;
        param.evse_maximum_voltage_limit = 920.0;
        res.dc_evse_charge_parameter = param;
        const auto result = handle_response(res);
        THEN("Done, and the SECC is flagged as stopping") {
            REQUIRE(result.action == Action::Done);
            REQUIRE(result.evse_stopping);
        }
    }

    GIVEN("A Finished response with a shutdown status code") {
        message_din::ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;
        dt::DcEvseChargeParameter param;
        param.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Shutdown;
        param.evse_maximum_current_limit = 400.0;
        param.evse_maximum_voltage_limit = 920.0;
        res.dc_evse_charge_parameter = param;
        const auto result = handle_response(res);
        THEN("Done, and the SECC is flagged as stopping") {
            REQUIRE(result.action == Action::Done);
            REQUIRE(result.evse_stopping);
        }
    }

    GIVEN("A Finished response with a ready EVSE and no stop notification") {
        message_din::ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;
        dt::DcEvseChargeParameter param;
        param.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        param.dc_evse_status.evse_notification = dt::EvseNotification::None;
        param.evse_maximum_current_limit = 400.0;
        param.evse_maximum_voltage_limit = 920.0;
        res.dc_evse_charge_parameter = param;
        const auto result = handle_response(res);
        THEN("Done, and the SECC is not flagged as stopping") {
            REQUIRE(result.action == Action::Done);
            REQUIRE(not result.evse_stopping);
        }
    }

    GIVEN("A Finished response without an explicit power limit") {
        message_din::ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EvseProcessing::Finished;
        dt::DcEvseChargeParameter param;
        param.evse_maximum_current_limit = 10.0;
        param.evse_maximum_voltage_limit = 100.0;
        res.dc_evse_charge_parameter = param;
        const auto result = handle_response(res);
        THEN("Power is derived from current * voltage") {
            REQUIRE(result.limits.has_value());
            REQUIRE(result.limits->power == 1000.0f);
        }
    }

    GIVEN("A failed response") {
        message_din::ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::FAILED_WrongEnergyTransferType;
        THEN("The action is Failed") {
            REQUIRE(handle_response(res).action == Action::Failed);
        }
    }
}
