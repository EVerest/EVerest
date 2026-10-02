// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

#include "common_types.hpp"

namespace iso15118::message_2 {

struct CurrentDemandRequest {
    Header header;
    datatypes::DC_EVStatus dc_ev_status;
    datatypes::PhysicalValue ev_target_current;
    std::optional<datatypes::PhysicalValue> ev_maximum_voltage_limit;
    std::optional<datatypes::PhysicalValue> ev_maximum_current_limit;
    std::optional<datatypes::PhysicalValue> ev_maximum_power_limit;
    std::optional<bool> bulk_charging_complete;
    bool charging_complete;
    std::optional<datatypes::PhysicalValue> remaining_time_to_full_soc;
    std::optional<datatypes::PhysicalValue> remaining_time_to_bulk_soc;
    datatypes::PhysicalValue ev_target_voltage;
};

struct CurrentDemandResponse {
    Header header;
    datatypes::ResponseCode response_code;
    datatypes::DC_EVSEStatus dc_evse_status;
    datatypes::PhysicalValue evse_present_voltage;
    datatypes::PhysicalValue evse_present_current;
    bool evse_current_limit_achieved;
    bool evse_voltage_limit_achieved;
    bool evse_power_limit_achieved;
    std::optional<datatypes::PhysicalValue> evse_maximum_voltage_limit;
    std::optional<datatypes::PhysicalValue> evse_maximum_current_limit;
    std::optional<datatypes::PhysicalValue> evse_maximum_power_limit;
    std::string evse_id;
    uint8_t sa_schedule_tuple_id;
    std::optional<datatypes::MeterInfo> meter_info;
    std::optional<bool> receipt_required;
};

} // namespace iso15118::message_2
