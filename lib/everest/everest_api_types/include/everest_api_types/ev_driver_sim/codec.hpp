// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "API.hpp"
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::ev_driver_sim {

std::string serialize(WorldFault val);
std::string serialize(PilotState val);
std::string serialize(CommandAckStatus val);
std::string serialize(ScenarioName val);
std::string serialize(ScenarioEventKind val);
std::string serialize(InjectFaultParams const& val) noexcept;
std::string serialize(WorldState const& val) noexcept;
std::string serialize(CommandAck const& val) noexcept;
std::string serialize(ScenarioTimingOverrides const& val) noexcept;
std::string serialize(RunScenarioParams const& val) noexcept;
std::string serialize(ScenarioEvent const& val) noexcept;

std::ostream& operator<<(std::ostream& os, WorldFault const& val);
std::ostream& operator<<(std::ostream& os, PilotState const& val);
std::ostream& operator<<(std::ostream& os, CommandAckStatus const& val);
std::ostream& operator<<(std::ostream& os, ScenarioName const& val);
std::ostream& operator<<(std::ostream& os, ScenarioEventKind const& val);
std::ostream& operator<<(std::ostream& os, InjectFaultParams const& val);
std::ostream& operator<<(std::ostream& os, WorldState const& val);
std::ostream& operator<<(std::ostream& os, CommandAck const& val);
std::ostream& operator<<(std::ostream& os, ScenarioTimingOverrides const& val);
std::ostream& operator<<(std::ostream& os, RunScenarioParams const& val);
std::ostream& operator<<(std::ostream& os, ScenarioEvent const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::ev_driver_sim
