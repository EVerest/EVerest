// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"

#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::utilities {

template <class T> std::string dump_json(T const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

template <class T> T parse_json(std::string_view val) {
    return nlohmann::json::parse(val.begin(), val.end());
}

} // namespace everest::lib::API::V1_0::types::utilities
