// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/common_types.hpp>

namespace iso15118::ev {

// True for every AC energy service. Single predicate so the AC/DC branches in the EV
// states, and in the module that configures them, cannot drift apart.
constexpr bool is_ac_family(message_20::datatypes::ServiceCategory service) {
    return service == message_20::datatypes::ServiceCategory::AC or
           service == message_20::datatypes::ServiceCategory::AC_BPT or
           service == message_20::datatypes::ServiceCategory::AC_DER_IEC;
}

} // namespace iso15118::ev
