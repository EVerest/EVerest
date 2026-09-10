// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/states.hpp>

namespace iso15118::ev::din::state {

// Charging loop. Leaves to PowerDelivery(Stop) on an EV stop or pause request, or when the SECC
// signals a stop.
struct CurrentDemand : public StateBase {
    CurrentDemand(Context& ctx) : StateBase(ctx, StateID::CurrentDemand) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::ev::din::state
