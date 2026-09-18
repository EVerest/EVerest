// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>

namespace iso15118::ev::d2::state {

struct SessionSetup : public StateBase {
    SessionSetup(Context& ctx) : StateBase(ctx, StateID::SessionSetup) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::ev::d2::state
