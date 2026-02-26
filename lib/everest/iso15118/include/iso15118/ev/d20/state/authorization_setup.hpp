// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct AuthorizationSetup : public StateBase {
public:
    AuthorizationSetup(Context& ctx) : StateBase(ctx, StateID::AuthorizationSetup) {
    }

    void enter() final;

    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
