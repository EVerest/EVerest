// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include "common_types.hpp"

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::message_2 {

namespace datatypes {

struct AC_EVChargeParameter {
    std::optional<uint32_t> departure_time;
    PhysicalValue e_amount;
    PhysicalValue ev_max_voltage;
    PhysicalValue ev_max_current;
    PhysicalValue ev_min_current;
};

struct DC_EVChargeParameter {
    std::optional<uint32_t> departure_time;
    DC_EVStatus dc_ev_status;
    PhysicalValue ev_maximum_current_limit;
    std::optional<PhysicalValue> ev_maximum_power_limit;
    PhysicalValue ev_maximum_voltage_limit;
    std::optional<PhysicalValue> ev_energy_capacity;
    std::optional<PhysicalValue> ev_energy_request;
    std::optional<int8_t> full_soc;
    std::optional<int8_t> bulk_soc;
};

struct PMaxScheduleEntry {
    uint32_t start;
    std::optional<uint32_t> duration;
    PhysicalValue p_max;
};

struct SAScheduleTuple {
    uint8_t sa_schedule_tuple_id;
    everest::lib::util::fixed_vector<PMaxScheduleEntry, 12> pmax_schedule;
};

using SAScheduleList = everest::lib::util::fixed_vector<SAScheduleTuple, 3>;

struct AC_EVSEChargeParameter {
    AC_EVSEStatus ac_evse_status;
    PhysicalValue evse_nominal_voltage;
    PhysicalValue evse_max_current;
};

struct DC_EVSEChargeParameter {
    DC_EVSEStatus dc_evse_status;
    PhysicalValue evse_maximum_current_limit;
    PhysicalValue evse_maximum_power_limit;
    PhysicalValue evse_maximum_voltage_limit;
    PhysicalValue evse_minimum_current_limit;
    PhysicalValue evse_minimum_voltage_limit;
    std::optional<PhysicalValue> evse_current_regulation_tolerance;
    PhysicalValue evse_peak_current_ripple;
    std::optional<PhysicalValue> evse_energy_to_be_delivered;
};

} // namespace datatypes

struct ChargeParameterDiscoveryRequest {
    Header header;
    std::optional<uint16_t> max_entries_sa_schedule_tuple;
    datatypes::EnergyTransferMode requested_energy_transfer_mode;
    std::optional<datatypes::AC_EVChargeParameter> ac_ev_charge_parameter;
    std::optional<datatypes::DC_EVChargeParameter> dc_ev_charge_parameter;
};

struct ChargeParameterDiscoveryResponse {
    Header header;
    datatypes::ResponseCode response_code;
    datatypes::EVSEProcessing evse_processing;
    std::optional<datatypes::SAScheduleList> sa_schedule_list;
    std::optional<datatypes::AC_EVSEChargeParameter> ac_evse_charge_parameter;
    std::optional<datatypes::DC_EVSEChargeParameter> dc_evse_charge_parameter;
};

} // namespace iso15118::message_2
