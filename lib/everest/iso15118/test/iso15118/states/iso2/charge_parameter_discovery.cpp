// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/state/charge_parameter_discovery.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {
d2::SessionConfig make_config() {
    d2::SessionConfig config;
    config.evse_id = "DE*PNX*E12345*1";
    config.supported_energy_transfer_modes.push_back(dt::EnergyTransferMode::DC_extended);
    config.supported_energy_transfer_modes.push_back(dt::EnergyTransferMode::AC_three_phase_core);
    config.dc_max_power = 150000.0f;
    config.dc_max_current = 300.0f;
    config.dc_max_voltage = 900.0f;
    config.ac_nominal_voltage = 230.0f;
    config.ac_max_current = 32.0f;
    return config;
}
} // namespace

SCENARIO("ISO 15118-2 SECC ChargeParameterDiscovery handling") {
    const dt::SessionId id{};
    const auto config = make_config();

    GIVEN("A DC request") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::DC_extended;
        req.dc_ev_charge_parameter.emplace();
        const auto res = d2::state::handle_request(req, id, config);
        THEN("OK, Finished, DC parameters and a valid SAScheduleList") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EVSEProcessing::Finished);
            REQUIRE(res.dc_evse_charge_parameter.has_value());
            REQUIRE_FALSE(res.ac_evse_charge_parameter.has_value());
            REQUIRE(res.sa_schedule_list.has_value());
            REQUIRE(res.sa_schedule_list->size() == 1);
            REQUIRE(res.sa_schedule_list->front().sa_schedule_tuple_id == 1);
            REQUIRE_FALSE(res.sa_schedule_list->front().pmax_schedule.empty());
        }
    }

    GIVEN("An AC request") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::AC_three_phase_core;
        req.ac_ev_charge_parameter.emplace();
        const auto res = d2::state::handle_request(req, id, config);
        THEN("OK, Finished, AC parameters and a valid SAScheduleList") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_processing == dt::EVSEProcessing::Finished);
            REQUIRE(res.ac_evse_charge_parameter.has_value());
            REQUIRE_FALSE(res.dc_evse_charge_parameter.has_value());
            REQUIRE(res.sa_schedule_list.has_value());
            REQUIRE(res.sa_schedule_list->front().sa_schedule_tuple_id == 1);
        }
    }

    GIVEN("An unsupported energy transfer mode") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.requested_energy_transfer_mode = dt::EnergyTransferMode::AC_single_phase_core;
        const auto res = d2::state::handle_request(req, id, config);
        THEN("FAILED_WrongEnergyTransferMode") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_WrongEnergyTransferMode);
        }
    }
}
