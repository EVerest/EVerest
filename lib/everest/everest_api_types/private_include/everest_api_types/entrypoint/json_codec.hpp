// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include "nlohmann/json_fwd.hpp"
#include <everest_api_types/entrypoint/API.hpp>

namespace everest::lib::API::V1_0::types::entrypoint {

using json = nlohmann::json;

void to_json(json& j, ApiTypeEnum const& k) noexcept;
void from_json(const json& j, ApiTypeEnum& k);

void to_json(json& j, ApiParameter const& k) noexcept;
void from_json(const json& j, ApiParameter& k);

void to_json(json& j, ApiDiscoverResponse const& k) noexcept;
void from_json(const json& j, ApiDiscoverResponse& k);

} // namespace everest::lib::API::V1_0::types::entrypoint
