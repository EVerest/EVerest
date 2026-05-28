// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once
#include <string>

#include "../states.hpp"

namespace iso15118::d2::state {

struct ChargeParameterDiscovery : public StateBase {
    ChargeParameterDiscovery(Context& ctx) : StateBase(ctx, StateID::ChargeParameterDiscovery) {
    }

    void enter() final {
        // TODO
    }

    Result feed(Event) final {
        // TODO
        return {};
    }
};

} // namespace iso15118::d2::state
