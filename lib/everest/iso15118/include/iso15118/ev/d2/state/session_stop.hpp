// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>

namespace iso15118::ev::d2::state {

// ChargingSession from Context::requested_stop_reason(); an accepted Pause pauses the session.
struct SessionStop : public StateBase {
    SessionStop(Context& ctx) : StateBase(ctx, StateID::SessionStop) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::ev::d2::state
