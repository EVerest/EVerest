// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::ev::state {

// Sends ChargeParameterDiscoveryReq (AC or DC parameters, by the requested mode) and polls until
// EVSEProcessing=Finished. Captures the SAScheduleTupleID, the first PMaxSchedule entry and the
// EVSE limits. DC -> CableCheck, AC -> PowerDelivery(Start).
struct ChargeParameterDiscovery : public StateBase {
    ChargeParameterDiscovery(Context& ctx) : StateBase(ctx, StateID::ChargeParameterDiscovery) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool first_request{true};
    bool ongoing_timeout_reached{false};

    void send(Event ev);
};

} // namespace iso15118::d2::ev::state
