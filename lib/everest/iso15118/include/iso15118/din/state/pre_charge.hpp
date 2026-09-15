// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <utility>

#include "../states.hpp"

namespace iso15118::din::state {

// A PreChargeReq arrives every 25-250 ms and each forward makes the module republish, so the target
// is forwarded on change only.
using PreChargeTarget = std::optional<std::pair<double, double>>;

// [V2G-DC-455]: a PowerDeliveryReq is not yet in sequence -- the EV owes at least one PreChargeReq.
struct PreChargeStart : public StateBase {
    PreChargeStart(Context& ctx) : StateBase(ctx, StateID::PreChargeStart) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_din::Variant& received) final;

private:
    PreChargeTarget forwarded_target{};
};

// [V2G-DC-458] widens the node to the PowerDeliveryReq that ends the phase.
struct PreCharge : public StateBase {
    // \p forwarded is handed over so the change filter is not reset by the transition.
    PreCharge(Context& ctx, PreChargeTarget forwarded) :
        StateBase(ctx, StateID::PreCharge), forwarded_target(std::move(forwarded)) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_din::Variant& received) final;

private:
    PreChargeTarget forwarded_target;
};

} // namespace iso15118::din::state
