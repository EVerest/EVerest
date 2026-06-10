// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "API.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::ev_board_support {

std::string serialize(EvCpState val) noexcept;
std::string serialize(BspMeasurement const& val) noexcept;

std::ostream& operator<<(std::ostream& os, EvCpState const& val);
std::ostream& operator<<(std::ostream& os, BspMeasurement const& val);

#include <everest_api_types/utilities/deserialize_templates.inc>

} // namespace everest::lib::API::V1_0::types::ev_board_support
