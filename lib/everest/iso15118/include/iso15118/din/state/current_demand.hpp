// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::din::state {

struct CurrentDemand : public StateBase {
    CurrentDemand(Context& ctx) : StateBase(ctx, StateID::CurrentDemand) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool charge_loop_started{false};
};

} // namespace iso15118::din::state
