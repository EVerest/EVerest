// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <nlohmann/json_fwd.hpp>

namespace ocpp {

/// \brief Converts \p j to an OCPP integer, a signed 32 bit value
/// \throws nlohmann::json::type_error if \p j is not a JSON integer
/// \throws nlohmann::json::out_of_range if \p j does not fit into 32 bits
std::int32_t integer_from_json(const nlohmann::json& j);

/// \brief Converts \p j to an OCPP decimal
/// \throws nlohmann::json::type_error if \p j is not a JSON number
/// \throws nlohmann::json::out_of_range if \p j is not finite or does not fit into a float
float decimal_from_json(const nlohmann::json& j);

} // namespace ocpp
