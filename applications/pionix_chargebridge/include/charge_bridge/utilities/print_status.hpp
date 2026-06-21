// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace charge_bridge::utilities {

enum class status_output_mode {
    auto_mode,
    log,
    terminal,
    off,
};

// Numeric telemetry from the heartbeat reply packet. Used by the interactive terminal UI for
// live readouts and sparklines; not part of the key=value log output.
struct chargebridge_telemetry {
    int cp_hi_mV{};
    int cp_lo_mV{};
    int pp_mOhm{};
    int temperature_mcu_C{};
    int temperature_pcb_C{};
    int vdd_12V_mV{};
    int vdd_N12V_mV{};
    int vdd_3v3_mV{};
};

struct chargebridge_status {
    std::string cb_name;
    bool connected{false};
    std::optional<bool> discovered;
    std::optional<bool> can0;
    std::optional<bool> serial1;
    std::optional<bool> serial2;
    std::optional<bool> serial3;
    std::optional<bool> plc;
    std::optional<bool> bsp;
    std::optional<bool> heartbeat;
    std::optional<bool> io;
    std::optional<int> mcu_resets;
    std::optional<chargebridge_telemetry> telemetry;
    // From the BSP bridge / IO packet; used by the interactive terminal UI only.
    std::optional<std::string> cp_state;
    std::optional<std::vector<int>> gpio;
    std::optional<std::vector<int>> adc;
    std::optional<std::vector<std::pair<std::string, int>>> io_telemetry;
};

void print_status(const chargebridge_status& status, status_output_mode output_mode = status_output_mode::terminal);

} // namespace charge_bridge::utilities
