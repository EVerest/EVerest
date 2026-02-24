// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once
#include <string>

#include "../states.hpp"

namespace iso15118::d2::state {

struct ServiceDiscovery : public StateBase {
    ServiceDiscovery(Context& ctx) : StateBase(ctx, StateID::ServiceDiscovery) {
    }

    void enter() final;

    Result feed(Event) final;

private:
    std::string evse_id;
};

} // namespace iso15118::d2::state
