// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include "API.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::lifecycle {

std::string serialize(StopModulesResultEnum val) noexcept;
std::string serialize(StartModulesResultEnum val) noexcept;
std::string serialize(ModuleExecutionStatusEnum val) noexcept;
std::string serialize(StopModulesResult const& val) noexcept;
std::string serialize(StartModulesResult const& val) noexcept;
std::string serialize(ConfigurationApiAvailability val) noexcept;
std::string serialize(ExecutionStatusUpdateNotice const& val) noexcept;
std::string serialize(EVerestVersion const& val) noexcept;

std::ostream& operator<<(std::ostream& os, StopModulesResultEnum const& val);
std::ostream& operator<<(std::ostream& os, StartModulesResultEnum const& val);
std::ostream& operator<<(std::ostream& os, ModuleExecutionStatusEnum const& val);
std::ostream& operator<<(std::ostream& os, StopModulesResult const& val);
std::ostream& operator<<(std::ostream& os, StartModulesResult const& val);
std::ostream& operator<<(std::ostream& os, ConfigurationApiAvailability const& val);
std::ostream& operator<<(std::ostream& os, ExecutionStatusUpdateNotice const& val);
std::ostream& operator<<(std::ostream& os, EVerestVersion const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::lifecycle
