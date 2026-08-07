// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

namespace charge_bridge::utilities {

enum class status_output_mode {
    auto_mode,
    log,
    terminal,
    off,
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
    std::optional<bool> gpio;
    std::optional<int> mcu_resets;
};

void print_status(const chargebridge_status& status, status_output_mode output_mode = status_output_mode::terminal);

} // namespace charge_bridge::utilities
