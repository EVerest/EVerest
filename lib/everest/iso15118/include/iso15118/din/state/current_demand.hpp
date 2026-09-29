// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <tuple>

#include "../states.hpp"

namespace iso15118::din::state {

// A CurrentDemandReq arrives every 25-250 ms and each forward makes the module republish, so the
// setpoint is forwarded on change only.
using EvSetpoint =
    std::optional<std::tuple<double, double, std::optional<double>, std::optional<double>, std::optional<double>>>;

// [V2G-DC-462]: a PowerDeliveryReq is not yet in sequence -- the EV owes at least one demand.
struct CurrentDemandStart : public StateBase {
    CurrentDemandStart(Context& ctx) : StateBase(ctx, StateID::CurrentDemandStart) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_din::Variant& received) final;

private:
    EvSetpoint forwarded_setpoint{};
};

// [V2G-DC-465] widens the node to the PowerDeliveryReq that ends charging.
struct CurrentDemand : public StateBase {
    // \p forwarded is handed over so the change filter is not reset by the transition.
    CurrentDemand(Context& ctx, EvSetpoint forwarded) :
        StateBase(ctx, StateID::CurrentDemand), forwarded_setpoint(std::move(forwarded)) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_din::Variant& received) final;

private:
    EvSetpoint forwarded_setpoint;
};

} // namespace iso15118::din::state
