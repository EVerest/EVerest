// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <utility>

#include "../states.hpp"

namespace iso15118::d2::state {

// PreChargeReq repeats every 25-250 ms and each forward makes the module republish, so the target is
// forwarded on change only.
using PreChargeTarget = std::optional<std::pair<double, double>>;

// [V2G2-584]: a PowerDeliveryReq is not in sequence until the EV has pre-charged at least once.
struct PreChargeStart : public StateBase {
    PreChargeStart(Context& ctx) : StateBase(ctx, StateID::PreChargeStart) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;

private:
    PreChargeTarget forwarded_target{};
};

// [V2G2-587] widens the node to the PowerDeliveryReq that starts delivery.
struct PreCharge : public StateBase {
    // \p forwarded is handed over so the change filter is not reset by the transition.
    PreCharge(Context& ctx, PreChargeTarget forwarded) :
        StateBase(ctx, StateID::PreCharge), forwarded_target(std::move(forwarded)) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;

private:
    PreChargeTarget forwarded_target;
};

} // namespace iso15118::d2::state
