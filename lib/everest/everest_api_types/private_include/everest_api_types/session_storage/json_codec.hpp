// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include "nlohmann/json_fwd.hpp"
#include <everest_api_types/session_storage/API.hpp>

namespace everest::lib::API::V1_0::types::session_storage {

using json = nlohmann::json;

void to_json(json& j, SessionState const& k) noexcept;
void from_json(const json& j, SessionState& k);

void to_json(json& j, Transaction const& k) noexcept;
void from_json(const json& j, Transaction& k);

void to_json(json& j, Session const& k) noexcept;
void from_json(const json& j, Session& k);

void to_json(json& j, SessionFilter const& k) noexcept;
void from_json(const json& j, SessionFilter& k);

void to_json(json& j, GetSessionsRequest const& k) noexcept;
void from_json(const json& j, GetSessionsRequest& k);

void to_json(json& j, SessionList const& k) noexcept;
void from_json(const json& j, SessionList& k);

void to_json(json& j, SessionIdentifier const& k) noexcept;
void from_json(const json& j, SessionIdentifier& k);

void to_json(json& j, ClearSessionsResult const& k) noexcept;
void from_json(const json& j, ClearSessionsResult& k);

} // namespace everest::lib::API::V1_0::types::session_storage
