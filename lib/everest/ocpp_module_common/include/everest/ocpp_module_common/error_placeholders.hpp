// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <string>
#include <string_view>

namespace Everest::error {
struct Error;
}

namespace ocpp_module_common {

/// \returns true if \p name, without the surrounding ${ and }, is substituted by substitute_error_placeholders
bool is_error_placeholder(std::string_view name);

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
