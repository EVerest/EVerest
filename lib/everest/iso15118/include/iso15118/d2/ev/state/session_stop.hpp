// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::ev::state {

// Terminal state. Sends SessionStopReq with ChargingSession=Pause when a pause is pending, otherwise
// Terminate. Marks the session paused/stopped accordingly.
struct SessionStop : public StateBase {
    SessionStop(Context& ctx) : StateBase(ctx, StateID::SessionStop) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::d2::ev::state
