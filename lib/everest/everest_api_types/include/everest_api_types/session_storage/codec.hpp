// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include "API.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::session_storage {

std::string serialize(SessionState val) noexcept;
std::string serialize(Transaction const& val) noexcept;
std::string serialize(Session const& val) noexcept;
std::string serialize(SessionFilter const& val) noexcept;
std::string serialize(GetSessionsRequest const& val) noexcept;
std::string serialize(SessionList const& val) noexcept;
std::string serialize(SessionIdentifier const& val) noexcept;
std::string serialize(ClearSessionsResult const& val) noexcept;

std::ostream& operator<<(std::ostream& os, SessionState const& val);
std::ostream& operator<<(std::ostream& os, Transaction const& val);
std::ostream& operator<<(std::ostream& os, Session const& val);
std::ostream& operator<<(std::ostream& os, SessionFilter const& val);
std::ostream& operator<<(std::ostream& os, GetSessionsRequest const& val);
std::ostream& operator<<(std::ostream& os, SessionList const& val);
std::ostream& operator<<(std::ostream& os, SessionIdentifier const& val);
std::ostream& operator<<(std::ostream& os, ClearSessionsResult const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::session_storage
