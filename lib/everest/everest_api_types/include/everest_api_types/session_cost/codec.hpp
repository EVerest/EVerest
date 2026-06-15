// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include "API.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::session_cost {

std::string serialize(DefaultPrice val) noexcept;
std::string serialize(TariffMessage val) noexcept;
std::string serialize(IdlePrice val) noexcept;
std::string serialize(CostCategory val) noexcept;
std::string serialize(ChargingPriceComponent val) noexcept;
std::string serialize(NextPeriodPrice val) noexcept;
std::string serialize(SessionCostChunk val) noexcept;
std::string serialize(SessionStatus val) noexcept;
std::string serialize(SessionCost val) noexcept;

std::ostream& operator<<(std::ostream& os, DefaultPrice const& val);
std::ostream& operator<<(std::ostream& os, TariffMessage const& val);
std::ostream& operator<<(std::ostream& os, IdlePrice const& val);
std::ostream& operator<<(std::ostream& os, CostCategory const& val);
std::ostream& operator<<(std::ostream& os, ChargingPriceComponent const& val);
std::ostream& operator<<(std::ostream& os, NextPeriodPrice const& val);
std::ostream& operator<<(std::ostream& os, SessionCostChunk const& val);
std::ostream& operator<<(std::ostream& os, SessionStatus const& val);
std::ostream& operator<<(std::ostream& os, SessionCost const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::session_cost
