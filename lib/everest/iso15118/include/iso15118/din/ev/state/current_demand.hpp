// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

#include "../config.hpp"

namespace iso15118::din::ev::state {

// Charging loop at a 500 ms message timeout. Leaves the loop on a StopCharging/PauseCharging control
// event or when the SECC signals a stop (DC_EVSEStatus not ready / EVSENotification StopCharging),
// transitioning to PowerDelivery(Stop).
struct CurrentDemand : public StateBase {
    CurrentDemand(Context& ctx) : StateBase(ctx, StateID::CurrentDemand) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool first_response{true};
    bool stop_requested{false};
    ChargingSession stop_reason{ChargingSession::Terminate};

    void send(Event ev);
};

} // namespace iso15118::din::ev::state
