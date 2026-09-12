// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/states.hpp>

namespace iso15118::ev::din::state {

// Voltage ramp: poll until EVSEPresentVoltage converges on the EV target, publish dc_power_on and
// advance to PowerDelivery(Start).
struct PreCharge : public StateBase {
    PreCharge(Context& ctx) : StateBase(ctx, StateID::PreCharge) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // A non-positive target never converges; the ongoing guard bounds the loop. Warn once.
    bool nonpositive_target_warned{false};
};

} // namespace iso15118::ev::din::state
