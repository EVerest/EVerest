// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::ev::state {

// DC PreCharge loop. Resends until the EVSE present voltage is within +/- 10 % of the target (Josev
// is_precharged, exclusive band). On convergence it publishes dc_power_on and advances to
// PowerDelivery(Start). Guarded by the pre-charge timeout.
struct PreCharge : public StateBase {
    PreCharge(Context& ctx) : StateBase(ctx, StateID::PreCharge) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool first_request{true};
    bool ongoing_timeout_reached{false};

    void send(Event ev);
    float target_voltage() const;
};

} // namespace iso15118::d2::ev::state
