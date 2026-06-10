// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once
#include "API.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::money {

std::string serialize(CurrencyCode val) noexcept;
std::string serialize(Currency val) noexcept;
std::string serialize(MoneyAmount val) noexcept;
std::string serialize(Price val) noexcept;

std::ostream& operator<<(std::ostream& os, CurrencyCode const& val);
std::ostream& operator<<(std::ostream& os, Currency const& val);
std::ostream& operator<<(std::ostream& os, MoneyAmount const& val);
std::ostream& operator<<(std::ostream& os, Price const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::money
