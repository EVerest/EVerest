// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/state/charge_parameter_discovery.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;

namespace {
din::SessionConfig make_config() {
    din::SessionConfig config;
    config.evse_maximum_current_limit = 400.0;
    config.evse_maximum_power_limit = 360000.0;
    config.evse_maximum_voltage_limit = 920.0;
    config.evse_minimum_current_limit = 0.0;
    config.evse_minimum_voltage_limit = 0.0;
    return config;
}
} // namespace

SCENARIO("DIN SECC ChargeParameterDiscovery state handling") {
    const dt::SessionId session{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("A DC_extended request, processing finished") {
        message_din::ChargeParameterDiscoveryRequest req;
        req.header.session_id = session;
        req.ev_requested_energy_transfer_type = dt::EnergyTransferMode::DC_extended;

        const auto res = din::state::handle_request(req, make_config(), true, session);
        THEN("The DC EVSE parameters are advertised and EVSEProcessing is Finished") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EvseProcessing::Finished);
            REQUIRE(res.dc_evse_charge_parameter.has_value());
            REQUIRE(res.dc_evse_charge_parameter->evse_maximum_current_limit == 400.0);
            REQUIRE(res.dc_evse_charge_parameter->evse_maximum_voltage_limit == 920.0);
            REQUIRE(res.dc_evse_charge_parameter->dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Ready);
        }
        THEN("A single-tuple SAScheduleList is advertised (mandatory when Finished)") {
            REQUIRE(res.sa_schedule_list.has_value());
            REQUIRE(res.sa_schedule_list->size() == 1);
            REQUIRE(res.sa_schedule_list->front().sa_schedule_tuple_id == 1);
            REQUIRE(res.sa_schedule_list->front().pmax_schedule.size() == 1);
            // 360 kW exceeds the DIN PMax short range and is capped at SHRT_MAX.
            REQUIRE(res.sa_schedule_list->front().pmax_schedule.front().p_max == 32767);
        }
    }

    GIVEN("A DC_extended request, processing ongoing (no schedule yet)") {
        message_din::ChargeParameterDiscoveryRequest req;
        req.header.session_id = session;
        req.ev_requested_energy_transfer_type = dt::EnergyTransferMode::DC_extended;

        const auto res = din::state::handle_request(req, make_config(), false, session);
        THEN("No SAScheduleList while Ongoing") {
            REQUIRE_FALSE(res.sa_schedule_list.has_value());
        }
    }

    GIVEN("A DC_extended request, processing ongoing") {
        message_din::ChargeParameterDiscoveryRequest req;
        req.header.session_id = session;
        req.ev_requested_energy_transfer_type = dt::EnergyTransferMode::DC_extended;

        const auto res = din::state::handle_request(req, make_config(), false, session);
        THEN("EVSEProcessing is Ongoing") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EvseProcessing::Ongoing);
        }
    }

    GIVEN("An AC request") {
        message_din::ChargeParameterDiscoveryRequest req;
        req.header.session_id = session;
        req.ev_requested_energy_transfer_type = dt::EnergyTransferMode::AC_single_phase_core;

        const auto res = din::state::handle_request(req, make_config(), true, session);
        THEN("ResponseCode is FAILED_WrongEnergyTransferType") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_WrongEnergyTransferType);
        }
    }
}
