// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest_api_types/over_voltage_monitor/API.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::over_voltage_monitor {

std::string serialize(ErrorEnum val) noexcept;
std::string serialize(ErrorSeverityEnum val) noexcept;
std::string serialize(Error const& val) noexcept;
std::string serialize(OverVoltageLimits const& val) noexcept;

std::ostream& operator<<(std::ostream& os, const Error& val);
std::ostream& operator<<(std::ostream& os, const ErrorEnum& val);
std::ostream& operator<<(std::ostream& os, const ErrorSeverityEnum& val);
std::ostream& operator<<(std::ostream& os, const OverVoltageLimits& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::over_voltage_monitor
