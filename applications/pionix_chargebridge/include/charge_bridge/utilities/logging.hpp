// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#pragma once
#include <functional>
#include <iostream>
#include <string>

namespace charge_bridge::utilities {

using print_error_sink = std::function<void(std::string)>;

std::ostream& print_error(std::string const& device, std::string const& unit, int status);
void set_print_error_sink(print_error_sink sink);
void clear_print_error_sink();

} // namespace charge_bridge::utilities
