// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>

namespace iso15118::ev::d20::state {

void DC_ChargeParameterDiscovery::enter() {
    // TODO(SL): Adding logging
}

Result DC_ChargeParameterDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    } else {
        return {};
    }
}

} // namespace iso15118::ev::d20::state
