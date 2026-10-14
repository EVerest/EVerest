// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "nlohmann/json_fwd.hpp"
#include <everest_api_types/ev_driver_sim/API.hpp>

namespace everest::lib::API::V1_0::types::ev_driver_sim {

using json = nlohmann::json;

void to_json(json& j, WorldFault const& k);
void from_json(const json& j, WorldFault& k);

void to_json(json& j, PilotState const& k);
void from_json(const json& j, PilotState& k);

void to_json(json& j, CommandAckStatus const& k);
void from_json(const json& j, CommandAckStatus& k);

void to_json(json& j, InjectFaultParams const& k) noexcept;
void from_json(const json& j, InjectFaultParams& k);

void to_json(json& j, WorldState const& k) noexcept;
void from_json(const json& j, WorldState& k);

void to_json(json& j, CommandAck const& k) noexcept;
void from_json(const json& j, CommandAck& k);

void to_json(json& j, ScenarioName const& k);

void from_json(const json& j, ScenarioName& k);

void to_json(json& j, ScenarioTimingOverrides const& k) noexcept;

void from_json(const json& j, ScenarioTimingOverrides& k);

void to_json(json& j, RunScenarioParams const& k) noexcept;

void from_json(const json& j, RunScenarioParams& k);

void to_json(json& j, ScenarioEventKind const& k);
void from_json(const json& j, ScenarioEventKind& k);
void to_json(json& j, ScenarioEvent const& k) noexcept;
void from_json(const json& j, ScenarioEvent& k);

} // namespace everest::lib::API::V1_0::types::ev_driver_sim
