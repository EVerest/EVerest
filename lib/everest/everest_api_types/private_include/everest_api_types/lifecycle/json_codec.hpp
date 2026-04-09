// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include "nlohmann/json_fwd.hpp"
#include <everest_api_types/lifecycle/API.hpp>

namespace everest::lib::API::V1_0::types::lifecycle {

using json = nlohmann::json;

void to_json(json& j, StopModulesResultEnum const& k) noexcept;
void from_json(const json& j, StopModulesResultEnum& k);

void to_json(json& j, StartModulesResultEnum const& k) noexcept;
void from_json(const json& j, StartModulesResultEnum& k);

void to_json(json& j, ModuleExecutionStatusEnum const& k) noexcept;
void from_json(const json& j, ModuleExecutionStatusEnum& k);

void to_json(json& j, StopModulesResult const& k) noexcept;
void from_json(const json& j, StopModulesResult& k);

void to_json(json& j, StartModulesResult const& k) noexcept;
void from_json(const json& j, StartModulesResult& k);

void to_json(json& j, ConfigurationApiAvailability const& k) noexcept;
void from_json(const json& j, ConfigurationApiAvailability& k);

void to_json(json& j, ExecutionStatusUpdateNotice const& k) noexcept;
void from_json(const json& j, ExecutionStatusUpdateNotice& k);

void to_json(json& j, EVerestVersion const& k) noexcept;
void from_json(const json& j, EVerestVersion& k);

} // namespace everest::lib::API::V1_0::types::lifecycle
