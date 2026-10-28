// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace everest::lib::API {

/// \brief Read a field declared as `format: date-time` in the AsyncAPI specs
///
/// Besides RFC 3339, accepts the deviations of everest::lib::datetime::parse_rfc3339_lenient(), so the modules behind
/// the API only ever see YYYY-MM-DDTHH:MM:SS.mmmZ.
/// \returns \p j as an RFC 3339 date-time in UTC with milliseconds
/// \throws nlohmann::json::type_error if \p j is no string, std::invalid_argument if it is no date-time
std::string date_time_from_json(const nlohmann::json& j);

} // namespace everest::lib::API
