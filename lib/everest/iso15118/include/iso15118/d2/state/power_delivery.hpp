// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_2/power_delivery.hpp>

#include "../states.hpp"

namespace iso15118::d2::state {

struct PowerDelivery : public StateBase {
    PowerDelivery(Context& ctx) : StateBase(ctx, StateID::PowerDelivery) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // Saved AC PowerDelivery(Start) request awaiting the contactor-closed control event.
    std::optional<message_2::PowerDeliveryRequest> saved_ac_start_req{std::nullopt};
};

} // namespace iso15118::d2::state
