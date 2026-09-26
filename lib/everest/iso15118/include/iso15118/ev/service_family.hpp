// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/common_types.hpp>

namespace iso15118::ev {

constexpr bool is_ac_family(message_20::datatypes::ServiceCategory service) {
    return service == message_20::datatypes::ServiceCategory::AC or
           service == message_20::datatypes::ServiceCategory::AC_BPT or
           service == message_20::datatypes::ServiceCategory::AC_DER_IEC;
}

constexpr bool is_dc_family(message_20::datatypes::ServiceCategory service) {
    return service == message_20::datatypes::ServiceCategory::DC or
           service == message_20::datatypes::ServiceCategory::DC_BPT;
}

// Selects the BPT transfer mode in parameter discovery and the BPT control modes in the charge
// loop, so it must not be written against a single category.
constexpr bool is_bpt(message_20::datatypes::ServiceCategory service) {
    return service == message_20::datatypes::ServiceCategory::AC_BPT or
           service == message_20::datatypes::ServiceCategory::DC_BPT;
}

} // namespace iso15118::ev
