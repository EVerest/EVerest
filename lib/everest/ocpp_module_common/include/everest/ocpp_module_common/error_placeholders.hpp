// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Everest::error {
struct Error;
}

namespace ocpp_module_common {

struct PlaceholderCheck {
    /// characters sent as written: everything except the placeholders that are substituted
    std::size_t static_length{0};
    /// placeholders without a matching error field, with ${ and }, e.g. "${mesage}"
    std::vector<std::string> unknown;
    /// a ${ without a closing }
    bool unterminated{false};
};

/// \returns what substitute_error_placeholders does with \p pattern, without an error at hand
PlaceholderCheck check_error_placeholders(std::string_view pattern);

/// \brief Replaces each ${name} in \p pattern with the matching field of \p error.
///
/// Supported names: type, sub_type, message, description, vendor_id, origin, origin_module,
/// origin_implementation, evse, connector, severity, state, timestamp, uuid.
/// evse and connector are empty when the origin has no mapping. Unknown names and unterminated
/// placeholders are kept verbatim, and substituted values are not scanned again.
///
/// \code
/// substitute_error_placeholders(error, "${type} raised by ${origin_module}");
/// \endcode
std::string substitute_error_placeholders(const Everest::error::Error& error, std::string_view pattern);

} // namespace ocpp_module_common
