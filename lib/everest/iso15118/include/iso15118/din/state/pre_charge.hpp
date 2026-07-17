// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::din::state {

struct PreCharge : public StateBase {
    PreCharge(Context& ctx) : StateBase(ctx, StateID::PreCharge) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool pre_charge_initiated{false};
};

} // namespace iso15118::din::state
