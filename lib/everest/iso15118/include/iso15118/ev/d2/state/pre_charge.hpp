// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>

namespace iso15118::ev::d2::state {

// DC PreCharge loop: resends until the EVSE present voltage converges on the target, then publishes
// dc_power_on and advances to PowerDelivery(Start).
struct PreCharge : public StateBase {
    PreCharge(Context& ctx) : StateBase(ctx, StateID::PreCharge) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // A non-positive target never converges; the ongoing guard bounds the loop. Warn once.
    bool nonpositive_target_warned{false};
};

} // namespace iso15118::ev::d2::state
