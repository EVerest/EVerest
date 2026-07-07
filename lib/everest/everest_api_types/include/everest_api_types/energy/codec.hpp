// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include "API.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::energy {

std::string serialize(NumberWithSource const& val) noexcept;
std::string serialize(IntegerWithSource const& val) noexcept;
std::string serialize(FrequencyWattPoint const& val) noexcept;
std::string serialize(SetpointType const& val) noexcept;
std::string serialize(PricePerkWh const& val) noexcept;
std::string serialize(LimitsReq const& val) noexcept;
std::string serialize(LimitsRes const& val) noexcept;
std::string serialize(ScheduleReqEntry const& val) noexcept;
std::string serialize(ScheduleResEntry const& val) noexcept;
std::string serialize(ScheduleSetpointEntry const& val) noexcept;
std::string serialize(ExternalLimits const& val) noexcept;
std::string serialize(EnforcedLimits const& val) noexcept;
std::string serialize(CapabilityLimits const& val) noexcept;
std::string serialize(NodeType const& val) noexcept;
std::string serialize(EvseState const& val) noexcept;
std::string serialize(OptimizerTarget const& val) noexcept;
std::string serialize(EnergyFlowRequest const& val) noexcept;

std::ostream& operator<<(std::ostream& os, NumberWithSource const& val);
std::ostream& operator<<(std::ostream& os, IntegerWithSource const& val);
std::ostream& operator<<(std::ostream& os, FrequencyWattPoint const& val);
std::ostream& operator<<(std::ostream& os, SetpointType const& val);
std::ostream& operator<<(std::ostream& os, PricePerkWh const& val);
std::ostream& operator<<(std::ostream& os, LimitsReq const& val);
std::ostream& operator<<(std::ostream& os, LimitsRes const& val);
std::ostream& operator<<(std::ostream& os, ScheduleReqEntry const& val);
std::ostream& operator<<(std::ostream& os, ScheduleResEntry const& val);
std::ostream& operator<<(std::ostream& os, ScheduleSetpointEntry const& val);
std::ostream& operator<<(std::ostream& os, ExternalLimits const& val);
std::ostream& operator<<(std::ostream& os, EnforcedLimits const& val);
std::ostream& operator<<(std::ostream& os, CapabilityLimits const& val);
std::ostream& operator<<(std::ostream& os, NodeType const& val);
std::ostream& operator<<(std::ostream& os, EvseState const& val);
std::ostream& operator<<(std::ostream& os, OptimizerTarget const& val);
std::ostream& operator<<(std::ostream& os, EnergyFlowRequest const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::energy
