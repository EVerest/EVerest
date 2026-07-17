// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "common_types.hpp"

namespace iso15118::message_din {

namespace datatypes {

// Minimal SAScheduleList representation. DIN 70121 PMax is a raw short (watts, capped at SHRT_MAX); the
// interval carries a start (0) and a mandatory duration. Only what a single advertised tuple needs.
struct PMaxScheduleEntry {
    uint32_t start{0};
    uint32_t duration{0};
    int16_t p_max{0};
};

struct SAScheduleTuple {
    int16_t sa_schedule_tuple_id{1};
    int16_t pmax_schedule_id{1};
    std::vector<PMaxScheduleEntry> pmax_schedule;
};

using SAScheduleList = std::vector<SAScheduleTuple>;

struct DcEvChargeParameter {
    DcEvStatus dc_ev_status;
    double ev_maximum_current_limit{0};
    std::optional<double> ev_maximum_power_limit;
    double ev_maximum_voltage_limit{0};
    std::optional<double> ev_energy_capacity;
    std::optional<double> ev_energy_request;
    std::optional<int8_t> full_soc;
    std::optional<int8_t> bulk_soc;
};

struct DcEvseChargeParameter {
    DcEvseStatus dc_evse_status;
    double evse_maximum_current_limit{0};
    std::optional<double> evse_maximum_power_limit;
    double evse_maximum_voltage_limit{0};
    double evse_minimum_current_limit{0};
    double evse_minimum_voltage_limit{0};
    std::optional<double> evse_current_regulation_tolerance;
    double evse_peak_current_ripple{0};
    std::optional<double> evse_energy_to_be_delivered;
};

} // namespace datatypes

struct ChargeParameterDiscoveryRequest {
    Header header;
    datatypes::EnergyTransferMode ev_requested_energy_transfer_type{datatypes::EnergyTransferMode::DC_core};
    std::optional<datatypes::DcEvChargeParameter> dc_ev_charge_parameter;
};

struct ChargeParameterDiscoveryResponse {
    Header header;
    datatypes::ResponseCode response_code;
    datatypes::EvseProcessing evse_processing{datatypes::EvseProcessing::Ongoing};
    std::optional<datatypes::SAScheduleList> sa_schedule_list;
    std::optional<datatypes::DcEvseChargeParameter> dc_evse_charge_parameter;
};

} // namespace iso15118::message_din
