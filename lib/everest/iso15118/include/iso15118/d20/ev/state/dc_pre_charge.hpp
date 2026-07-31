// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d20::ev::state {

// Ongoing polling loop: the EV decides convergence (EVSE present voltage within +/- 10 % of the
// target). Once converged it sends ev_processing=Finished; on completion it publishes dc_power_on
// and transitions to PowerDelivery(Start). Guarded by V2G_EVCC_PreCharge_Timeout.
struct DC_PreCharge : public StateBase {
    DC_PreCharge(Context& ctx) : StateBase(ctx, StateID::DC_PreCharge) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool first_request{true};
    bool converged{false};
    bool sent_finished{false};
    bool ongoing_timeout_reached{false};

    void send(Event ev);
    float target_voltage() const;
};

} // namespace iso15118::d20::ev::state
