// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest_api_types/generic/API.hpp>

#include <optional>

#include <utils/error.hpp>

namespace everest::lib::API {

/// \returns the EVerest error attribute described by \p value, or std::nullopt when \p value is
///          absent or names no actual value
///
/// \code
/// ev_error.caused_by_value = to_caused_by_value(payload.caused_by_value);
/// \endcode
inline std::optional<Everest::error::CausedByValue>
to_caused_by_value(const std::optional<V1_0::types::generic::CausedByValue>& value) {
    if (not value.has_value() or not value.value().actual_value.has_value()) {
        return std::nullopt;
    }
    return Everest::error::CausedByValue{value.value().actual_value.value()};
}

} // namespace everest::lib::API
