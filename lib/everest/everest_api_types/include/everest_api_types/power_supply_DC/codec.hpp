// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest_api_types/power_supply_DC/API.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::power_supply_DC {

std::string serialize(Capabilities const& val) noexcept;
std::string serialize(Mode val) noexcept;
std::string serialize(ModeRequest val) noexcept;
std::string serialize(ChargingPhase val) noexcept;
std::string serialize(VoltageCurrent const& val) noexcept;
std::string serialize(ErrorEnum val) noexcept;
std::string serialize(Error const& val) noexcept;

std::ostream& operator<<(std::ostream& os, const Capabilities& val);
std::ostream& operator<<(std::ostream& os, const Mode& val);
std::ostream& operator<<(std::ostream& os, const ChargingPhase& val);
std::ostream& operator<<(std::ostream& os, const ModeRequest& val);
std::ostream& operator<<(std::ostream& os, const VoltageCurrent& val);
std::ostream& operator<<(std::ostream& os, const Error& val);
std::ostream& operator<<(std::ostream& os, const ErrorEnum& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::power_supply_DC
