// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include "API.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::error_history {

std::string serialize(State val) noexcept;
std::string serialize(SeverityFilter val) noexcept;
std::string serialize(Severity val) noexcept;
std::string serialize(ImplementationIdentifier const& val) noexcept;
std::string serialize(TimeperiodFilter const& val) noexcept;
std::string serialize(FilterArguments const& val) noexcept;
std::string serialize(ErrorObject const& val) noexcept;
std::string serialize(ErrorList const& val) noexcept;

std::ostream& operator<<(std::ostream& os, State const& val);
std::ostream& operator<<(std::ostream& os, SeverityFilter const& val);
std::ostream& operator<<(std::ostream& os, Severity const& val);
std::ostream& operator<<(std::ostream& os, ImplementationIdentifier const& val);
std::ostream& operator<<(std::ostream& os, TimeperiodFilter const& val);
std::ostream& operator<<(std::ostream& os, FilterArguments const& val);
std::ostream& operator<<(std::ostream& os, ErrorObject const& val);
std::ostream& operator<<(std::ostream& os, ErrorList const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::error_history
