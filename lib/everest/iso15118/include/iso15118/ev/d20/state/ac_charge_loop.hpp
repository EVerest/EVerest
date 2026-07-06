// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct AC_ChargeLoop : public StateBase {
public:
    AC_ChargeLoop(Context& ctx) : StateBase(ctx, StateID::AC_ChargeLoop) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
