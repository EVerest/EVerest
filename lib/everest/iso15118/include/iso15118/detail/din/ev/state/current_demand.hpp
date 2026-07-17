// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_din/current_demand.hpp>

namespace iso15118::din::ev::state {

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

enum class ChargerState {
    Continue,
    Stop,
};

struct Result {
    bool valid{false};
    ChargerState charger_state{ChargerState::Continue};
    float present_voltage{0.0f};
    float present_current{0.0f};
};

Result handle_response(const message_din::CurrentDemandResponse& res);

} // namespace current_demand

} // namespace iso15118::din::ev::state
