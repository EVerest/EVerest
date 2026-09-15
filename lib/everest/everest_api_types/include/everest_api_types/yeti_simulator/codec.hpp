// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "API.hpp"
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::yeti_simulator {

std::string serialize(Severity val);

std::string serialize(RaiseError const& val) noexcept;
std::string serialize(ClearError const& val) noexcept;

std::ostream& operator<<(std::ostream& os, Severity const& val);

std::ostream& operator<<(std::ostream& os, RaiseError const& val);
std::ostream& operator<<(std::ostream& os, ClearError const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::yeti_simulator
