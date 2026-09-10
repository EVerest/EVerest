// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_2/power_delivery.hpp>

#include "../states.hpp"

namespace iso15118::d2::state {

// An AC session has to close the contactor and wait for the confirmation before it may answer; a DC
// session answers straight away and ends into WeldingDetection rather than SessionStop.
// Only the AC side is a state. On DC the request is accepted inside PreCharge and DcChargeLoop and
// processed on the transition out, so the DC half is process_dc_power_delivery().

// AC: PowerDelivery(Start) closes the contactor and the response waits for the confirmation.
struct AcPowerDelivery : public StateBase {
    AcPowerDelivery(Context& ctx) : StateBase(ctx, StateID::AcPowerDelivery) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;

private:
    // PowerDelivery(Start) held back until the ClosedContactor control event arrives.
    std::optional<message_2::PowerDeliveryRequest> saved_start_req{std::nullopt};
};

} // namespace iso15118::d2::state
