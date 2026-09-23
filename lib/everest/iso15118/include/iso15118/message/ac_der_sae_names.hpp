// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <string_view>

#include "ac_der_sae_types.hpp"

namespace iso15118::message_20::datatypes::sae {

// Empty for a value that is no enumerator.
std::string_view to_string(IEEE1547NormalCategory value);
std::string_view to_string(IEEE1547AbnormalCategory value);
std::string_view to_string(DEROperationalState value);
std::string_view to_string(DERConnectionStatus value);

} // namespace iso15118::message_20::datatypes::sae
