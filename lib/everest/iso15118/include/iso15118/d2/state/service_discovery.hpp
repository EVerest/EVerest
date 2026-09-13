// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::state {

struct ServiceDiscovery : public StateBase {
    ServiceDiscovery(Context& ctx) : StateBase(ctx, StateID::ServiceDiscovery) {
    }

    void enter() final;
    Result on_request(const message_2::Variant& received) final;
};

} // namespace iso15118::d2::state
