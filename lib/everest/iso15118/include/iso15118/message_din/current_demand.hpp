// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include "common_types.hpp"

namespace iso15118::message_din {

struct CurrentDemandRequest {
    Header header;
    datatypes::DcEvStatus dc_ev_status;
    double ev_target_current{0};
    double ev_target_voltage{0};
    std::optional<double> ev_maximum_voltage_limit;
    std::optional<double> ev_maximum_current_limit;
    std::optional<double> ev_maximum_power_limit;
    std::optional<bool> bulk_charging_complete;
    bool charging_complete{false};
    std::optional<double> remaining_time_to_full_soc;
    std::optional<double> remaining_time_to_bulk_soc;
};

struct CurrentDemandResponse {
    Header header;
    datatypes::ResponseCode response_code;
    datatypes::DcEvseStatus dc_evse_status;
    double evse_present_voltage{0};
    double evse_present_current{0};
    bool evse_current_limit_achieved{false};
    bool evse_voltage_limit_achieved{false};
    bool evse_power_limit_achieved{false};
    std::optional<double> evse_maximum_voltage_limit;
    std::optional<double> evse_maximum_current_limit;
    std::optional<double> evse_maximum_power_limit;
};

} // namespace iso15118::message_din
