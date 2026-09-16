// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct DC_CableCheck : public StateBase {
public:
    DC_CableCheck(Context& ctx) : StateBase(ctx, StateID::DC_CableCheck) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // [V2G2-847]: with CP-state feedback the first request waits for state C or D.
    bool request_sent{false};
};

} // namespace iso15118::ev::d20::state
