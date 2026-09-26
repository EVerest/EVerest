// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/der_sae_control_validation.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct AC_DER_SAE_ChargeLoop : public StateBase {
public:
    AC_DER_SAE_ChargeLoop(Context& ctx) : StateBase(ctx, StateID::AC_DER_SAE_ChargeLoop) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    DerControlProblems last_problems_;
};

} // namespace iso15118::ev::d20::state
