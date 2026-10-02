// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct AC_DER_IEC_ChargeLoop : public StateBase {
public:
    AC_DER_IEC_ChargeLoop(Context& ctx) : StateBase(ctx, StateID::AC_DER_IEC_ChargeLoop) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::ev::d20::state
