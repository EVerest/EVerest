// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/service_discovery.hpp>

#include <iso15118/detail/d2/context_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

void ServiceDiscovery::enter() {
    // m_ctx.log.enter_state("ServiceDiscovery");
}

Result ServiceDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    return {};
}

} // namespace iso15118::d2::state
