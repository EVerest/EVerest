// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#pragma once
#include <functional>
#include <iostream>
#include <string>

namespace charge_bridge::utilities {

// The sink receives the originating charge_bridge name (the "device", may be empty for
// non-instance-specific messages) and the fully formatted message line.
using print_error_sink = std::function<void(std::string device, std::string message)>;

// Whether diagnostics written to stdout (print_error/print_info without a sink, and the key=value
// status lines) may use ANSI colors. Defaults to "stdout is a TTY", so escapes never end up in a
// redirected log by accident; --status-no-color turns them off for a TTY too. Set once during
// startup, read from every logging thread.
void set_diagnostic_color_enabled(bool enabled);
bool diagnostic_color_enabled();

std::ostream& print_error(std::string const& device, std::string const& unit, int status);
// Informational log line (no WARNING/ERROR level). Routes through the print_error sink when one is
// installed (so it shows in the terminal UI's message panel instead of being painted over by the
// redraw), otherwise writes to stdout like print_error. Use for non-error diagnostic output.
std::ostream& print_info(std::string const& device, std::string const& unit);
void set_print_error_sink(print_error_sink sink);
void clear_print_error_sink();

} // namespace charge_bridge::utilities
