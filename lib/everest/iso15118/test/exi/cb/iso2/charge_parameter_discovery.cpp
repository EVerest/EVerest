// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/variant.hpp>

#include "helper.hpp"

using namespace iso15118;
using namespace iso15118::message_2::datatypes;

SCENARIO("Se/Deserialize ISO-2 charge parameter discovery messages") {

    GIVEN("Round-trip AC charge_parameter_discovery_req") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.max_entries_sa_schedule_tuple = 10;
        req.requested_energy_transfer_mode = EnergyTransferMode::AC_three_phase_core;
        auto& ac = req.ac_ev_charge_parameter.emplace();
        ac.e_amount = to_physical_value(20000, Unit::Wh);
        ac.ev_max_voltage = to_physical_value(400, Unit::V);
        ac.ev_max_current = to_physical_value(32, Unit::A);
        ac.ev_min_current = to_physical_value(10, Unit::A);

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::ChargeParameterDiscoveryReq);
            const auto& msg = variant.get<message_2::ChargeParameterDiscoveryRequest>();
            REQUIRE(msg.requested_energy_transfer_mode == EnergyTransferMode::AC_three_phase_core);
            REQUIRE(msg.ac_ev_charge_parameter.has_value());
            REQUIRE(not msg.dc_ev_charge_parameter.has_value());
            REQUIRE(from_physical_value(msg.ac_ev_charge_parameter->ev_max_current) == 32);
            REQUIRE(from_physical_value(msg.ac_ev_charge_parameter->ev_max_voltage) == 400);
        }
    }

    GIVEN("Round-trip DC charge_parameter_discovery_req") {
        message_2::ChargeParameterDiscoveryRequest req;
        req.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        req.requested_energy_transfer_mode = EnergyTransferMode::DC_extended;
        auto& dc = req.dc_ev_charge_parameter.emplace();
        dc.dc_ev_status = {true, DC_EVErrorCode::NO_ERROR, 50};
        dc.ev_maximum_current_limit = to_physical_value(200, Unit::A);
        dc.ev_maximum_voltage_limit = to_physical_value(500, Unit::V);
        dc.ev_maximum_power_limit = to_physical_value(50000, Unit::W);
        dc.full_soc = 100;

        const auto serialized = serialize_helper(req);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::ChargeParameterDiscoveryReq);
            const auto& msg = variant.get<message_2::ChargeParameterDiscoveryRequest>();
            REQUIRE(msg.requested_energy_transfer_mode == EnergyTransferMode::DC_extended);
            REQUIRE(msg.dc_ev_charge_parameter.has_value());
            REQUIRE(msg.dc_ev_charge_parameter->dc_ev_status.ev_ress_soc == 50);
            REQUIRE(msg.dc_ev_charge_parameter->ev_maximum_power_limit.has_value());
            REQUIRE(from_physical_value(msg.dc_ev_charge_parameter->ev_maximum_current_limit) == 200);
            REQUIRE(msg.dc_ev_charge_parameter->full_soc.has_value());
            REQUIRE(msg.dc_ev_charge_parameter->full_soc.value() == 100);
        }
    }

    GIVEN("Round-trip DC charge_parameter_discovery_res with SAScheduleList") {
        message_2::ChargeParameterDiscoveryResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;
        res.evse_processing = EVSEProcessing::Finished;

        auto& list = res.sa_schedule_list.emplace();
        auto& tuple = list.emplace_back();
        tuple.sa_schedule_tuple_id = 1;
        auto& entry = tuple.pmax_schedule.emplace_back();
        entry.start = 0;
        entry.duration = 3600;
        entry.p_max = to_physical_value(11000, Unit::W);

        auto& dc = res.dc_evse_charge_parameter.emplace();
        dc.dc_evse_status = {0, EVSENotification::None, IsolationLevel::Valid, DC_EVSEStatusCode::EVSE_Ready};
        dc.evse_maximum_current_limit = to_physical_value(200, Unit::A);
        dc.evse_maximum_power_limit = to_physical_value(50000, Unit::W);
        dc.evse_maximum_voltage_limit = to_physical_value(500, Unit::V);
        dc.evse_minimum_current_limit = to_physical_value(0, Unit::A);
        dc.evse_minimum_voltage_limit = to_physical_value(150, Unit::V);
        dc.evse_peak_current_ripple = to_physical_value(1, Unit::A);

        const auto serialized = serialize_helper(res);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::ChargeParameterDiscoveryRes);
            const auto& msg = variant.get<message_2::ChargeParameterDiscoveryResponse>();
            REQUIRE(msg.response_code == ResponseCode::OK);
            REQUIRE(msg.evse_processing == EVSEProcessing::Finished);
            REQUIRE(msg.sa_schedule_list.has_value());
            REQUIRE(msg.sa_schedule_list->size() == 1);
            REQUIRE(msg.sa_schedule_list->at(0).sa_schedule_tuple_id == 1);
            REQUIRE(msg.sa_schedule_list->at(0).pmax_schedule.size() == 1);
            REQUIRE(msg.sa_schedule_list->at(0).pmax_schedule[0].duration.has_value());
            REQUIRE(msg.sa_schedule_list->at(0).pmax_schedule[0].duration.value() == 3600);
            REQUIRE(msg.dc_evse_charge_parameter.has_value());
            REQUIRE(msg.dc_evse_charge_parameter->dc_evse_status.isolation_status.has_value());
            REQUIRE(msg.dc_evse_charge_parameter->dc_evse_status.isolation_status.value() == IsolationLevel::Valid);
            REQUIRE(from_physical_value(msg.dc_evse_charge_parameter->evse_maximum_voltage_limit) == 500);
        }
    }

    GIVEN("Round-trip AC charge_parameter_discovery_res") {
        message_2::ChargeParameterDiscoveryResponse res;
        res.header.session_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
        res.response_code = ResponseCode::OK;
        res.evse_processing = EVSEProcessing::Finished;

        auto& ac = res.ac_evse_charge_parameter.emplace();
        ac.ac_evse_status = {0, EVSENotification::None, false};
        ac.evse_nominal_voltage = to_physical_value(230, Unit::V);
        ac.evse_max_current = to_physical_value(32, Unit::A);

        const auto serialized = serialize_helper(res);
        const io::StreamInputView stream_view{serialized.data(), serialized.size()};
        message_2::Variant variant(stream_view);

        THEN("It should be deserialized successfully") {
            REQUIRE(variant.get_type() == message_2::Type::ChargeParameterDiscoveryRes);
            const auto& msg = variant.get<message_2::ChargeParameterDiscoveryResponse>();
            REQUIRE(msg.ac_evse_charge_parameter.has_value());
            REQUIRE(from_physical_value(msg.ac_evse_charge_parameter->evse_nominal_voltage) == 230);
            REQUIRE(from_physical_value(msg.ac_evse_charge_parameter->evse_max_current) == 32);
        }
    }
}
