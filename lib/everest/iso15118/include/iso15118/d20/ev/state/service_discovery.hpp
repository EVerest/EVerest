// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d20::ev::state {

struct ServiceDiscovery : public StateBase {
    ServiceDiscovery(Context& ctx) : StateBase(ctx, StateID::ServiceDiscovery) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::d20::ev::state
