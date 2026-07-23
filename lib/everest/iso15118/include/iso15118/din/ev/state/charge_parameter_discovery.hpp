// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::din::ev::state {

// Ongoing polling loop: resend the DC ChargeParameterDiscoveryReq until EVSEProcessing=Finished. On
// completion it stores the EVSE limits, publishes ev_power_ready(true) and dc_evse_present_limits, then
// advances to CableCheck. Guarded by V2G_SECC_SequenceTimeout.
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

} // namespace iso15118::din::ev::state
