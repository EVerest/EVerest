// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d20/state/dc_pre_charge.hpp>

namespace iso15118::ev::d20::state {

void DC_PreCharge::enter() {
}

Result DC_PreCharge::feed(Event) {
    // Stub: full implementation tracked separately. Until then, transitions land here and stay.
    return {};
}

} // namespace iso15118::ev::d20::state
