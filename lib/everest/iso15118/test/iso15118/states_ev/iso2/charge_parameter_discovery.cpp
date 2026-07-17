// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/ev/state/charge_parameter_discovery.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;
namespace cpd = d2::ev::state::charge_parameter_discovery;

namespace {
d2::ev::EvSessionConfig make_config(dt::EnergyTransferMode mode) {
    d2::ev::EvSessionConfig config;
    config.requested_energy_transfer_mode = mode;
    return config;
}
} // namespace

SCENARIO("EVCC ISO-2 ChargeParameterDiscovery request variants") {

    GIVEN("A DC config") {
        dt::DC_EVStatus status;
        status.ev_ready = true;
        status.ev_ress_soc = 60;
        const auto req = cpd::create_dc_request(make_config(dt::EnergyTransferMode::DC_extended), status);
        THEN("Only the DC parameter set is populated with the requested mode and the SoC is echoed") {
            REQUIRE(req.requested_energy_transfer_mode == dt::EnergyTransferMode::DC_extended);
            REQUIRE(req.dc_ev_charge_parameter.has_value());
            REQUIRE(not req.ac_ev_charge_parameter.has_value());
            REQUIRE(req.dc_ev_charge_parameter->dc_ev_status.ev_ready);
            REQUIRE(req.dc_ev_charge_parameter->dc_ev_status.ev_ress_soc == 60);
        }
    }

    GIVEN("An AC config") {
        const auto req = cpd::create_ac_request(make_config(dt::EnergyTransferMode::AC_three_phase_core));
        THEN("Only the AC parameter set is populated with the requested mode") {
            REQUIRE(req.requested_energy_transfer_mode == dt::EnergyTransferMode::AC_three_phase_core);
            REQUIRE(req.ac_ev_charge_parameter.has_value());
            REQUIRE(not req.dc_ev_charge_parameter.has_value());
        }
    }
}

SCENARIO("EVCC ISO-2 ChargeParameterDiscovery response handling") {

    GIVEN("An Ongoing response") {
        message_2::ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EVSEProcessing::Ongoing;
        const auto result = cpd::handle_response(res);
        THEN("It is valid but not finished") {
            REQUIRE(result.valid);
            REQUIRE(not result.finished);
        }
    }

    GIVEN("A Finished DC response with schedule and limits") {
        message_2::ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EVSEProcessing::Finished;

        dt::SAScheduleTuple tuple;
        tuple.sa_schedule_tuple_id = 5;
        dt::PMaxScheduleEntry entry;
        entry.start = 0;
        entry.p_max = dt::to_physical_value(30000.0, dt::Unit::W);
        tuple.pmax_schedule.push_back(entry);
        dt::PMaxScheduleEntry entry2;
        entry2.start = 1800;
        entry2.p_max = dt::to_physical_value(15000.0, dt::Unit::W);
        tuple.pmax_schedule.push_back(entry2);
        dt::SAScheduleList list;
        list.push_back(tuple);
        res.sa_schedule_list = list;

        dt::DC_EVSEChargeParameter dc;
        dc.evse_maximum_voltage_limit = dt::to_physical_value(920.0, dt::Unit::V);
        dc.evse_maximum_current_limit = dt::to_physical_value(400.0, dt::Unit::A);
        dc.evse_maximum_power_limit = dt::to_physical_value(360000.0, dt::Unit::W);
        res.dc_evse_charge_parameter = dc;

        const auto result = cpd::handle_response(res);
        THEN("The schedule tuple id, full PMax schedule and DC limits are captured") {
            REQUIRE(result.finished);
            REQUIRE(result.sa_schedule_tuple_id.has_value());
            REQUIRE(result.sa_schedule_tuple_id.value() == 5);
            REQUIRE(result.selected_pmax_entry.has_value());
            REQUIRE(result.selected_pmax_schedule.size() == 2);
            REQUIRE(result.selected_pmax_schedule[1].start == 1800);
            REQUIRE(result.dc_limits.has_value());
            REQUIRE(result.dc_limits->voltage > 900.0f);
            REQUIRE(not result.ac_nominal_voltage.has_value());
        }
    }

    GIVEN("A Finished AC response with nominal voltage") {
        message_2::ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_processing = dt::EVSEProcessing::Finished;
        dt::AC_EVSEChargeParameter ac;
        ac.evse_nominal_voltage = dt::to_physical_value(230.0, dt::Unit::V);
        ac.evse_max_current = dt::to_physical_value(32.0, dt::Unit::A);
        res.ac_evse_charge_parameter = ac;

        const auto result = cpd::handle_response(res);
        THEN("The AC nominal voltage and max current are captured") {
            REQUIRE(result.finished);
            REQUIRE(result.ac_nominal_voltage.has_value());
            REQUIRE(result.ac_max_current.has_value());
            REQUIRE(not result.dc_limits.has_value());
        }
    }

    GIVEN("A failed response") {
        message_2::ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::FAILED_WrongChargeParameter;
        const auto result = cpd::handle_response(res);
        THEN("It is invalid") {
            REQUIRE(not result.valid);
        }
    }
}
