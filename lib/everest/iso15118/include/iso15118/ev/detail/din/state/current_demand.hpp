// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_din/current_demand.hpp>

namespace iso15118::ev::din::state {

namespace dt = message_din::datatypes;

namespace current_demand {

struct RequestParams {
    dt::DcEvStatus dc_ev_status;
    double target_voltage{0.0};
    double target_current{0.0};
    std::optional<double> max_voltage_limit{std::nullopt};
    std::optional<double> max_current_limit{std::nullopt};
    std::optional<double> max_power_limit{std::nullopt};
    bool charging_complete{false};
};

message_din::CurrentDemandRequest create_request(const RequestParams& params);

// [V2G-DC-637]: status codes without explicit requirements are informational; EVSE_IsolationMonitoringActive
// and EVSE_Malfunction stay set in real SECCs. [V2G-DC-650]: EVSE_Shutdown / EVSE_EmergencyShutdown and
// EVSENotification StopCharging stop charging.
bool charger_requests_stop(const message_din::CurrentDemandResponse& res);

} // namespace current_demand

} // namespace iso15118::ev::din::state
