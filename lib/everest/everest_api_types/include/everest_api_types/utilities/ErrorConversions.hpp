// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest_api_types/generic/API.hpp>

#include <optional>

#include <utils/error.hpp>

namespace everest::lib::API {

/// \returns the EVerest error attribute described by \p value, or std::nullopt when \p value is
///          absent or carries no text representation
///
/// \code
/// error_factory->create_error(type, sub_type, message, severity, to_trigger_value(payload.trigger_value));
/// \endcode
inline std::optional<Everest::error::TriggerValue>
to_trigger_value(const std::optional<V1_0::types::generic::TriggerValue>& value) {
    if (not value.has_value() or not value.value().text_repr.has_value()) {
        return std::nullopt;
    }
    return Everest::error::TriggerValue{value.value().text_repr.value()};
}

} // namespace everest::lib::API
