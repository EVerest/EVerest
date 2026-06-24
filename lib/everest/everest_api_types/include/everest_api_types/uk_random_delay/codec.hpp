// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "API.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::uk_random_delay {

std::string serialize(CountDown const& val) noexcept;

std::ostream& operator<<(std::ostream& os, CountDown const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::uk_random_delay
