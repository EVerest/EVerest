// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/schedule_exchange.hpp>

#include "../states.hpp"

namespace iso15118::d20::ev::state {

// Shared AC + DC state. Selects Dynamic vs Scheduled from the negotiated control mode and, on
// Finished, publishes ev_power_ready(true) then branches to DC_CableCheck (DC) or PowerDelivery (AC).
struct ScheduleExchange : public StateBase {
    ScheduleExchange(Context& ctx) : StateBase(ctx, StateID::ScheduleExchange) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // Cached so the request can be resent unaltered while the SECC keeps replying Ongoing.
    message_20::ScheduleExchangeRequest cached_request;
    bool first_request{true};

    void send(Event ev);
};

} // namespace iso15118::d20::ev::state
