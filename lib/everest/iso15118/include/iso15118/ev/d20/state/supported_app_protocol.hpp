// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../context.hpp"
#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct SupportedAppProtocol : public StateBase {
    SupportedAppProtocol(Context& ctx) : StateBase(ctx, StateID::SupportedAppProtocol) {
    }

    void enter() final;

    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
