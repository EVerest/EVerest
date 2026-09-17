// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/session_stop.hpp>

#include "../states.hpp"

namespace iso15118::d2::state {

struct SessionStop : public StateBase {
    SessionStop(Context& ctx) : StateBase(ctx, StateID::SessionStop) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;
};

} // namespace iso15118::d2::state
