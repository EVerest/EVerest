// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include "nlohmann/json_fwd.hpp"
#include <everest_api_types/yeti_simulator/API.hpp>

namespace everest::lib::API::V1_0::types::yeti_simulator {

using json = nlohmann::json;

void to_json(json& j, Severity const& k);
void from_json(const json& j, Severity& k);

void to_json(json& j, RaiseError const& k) noexcept;
void from_json(const json& j, RaiseError& k);

void to_json(json& j, ClearError const& k) noexcept;
void from_json(const json& j, ClearError& k);

} // namespace everest::lib::API::V1_0::types::yeti_simulator
