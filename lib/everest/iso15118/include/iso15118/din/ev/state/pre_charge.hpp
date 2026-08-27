// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::din::ev::state {

// Ongoing polling loop: the EV ramps the EVSE output voltage. Once the EVSE present voltage is within
// +/- 10 % of the EV target voltage (and within an absolute 20 V cap) it publishes dc_power_on() and
// advances to PowerDelivery(Start). Guarded by V2G_EVCC_PreCharge_Timeout.
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

} // namespace iso15118::din::ev::state
