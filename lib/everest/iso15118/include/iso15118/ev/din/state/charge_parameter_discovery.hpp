// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/states.hpp>

namespace iso15118::ev::din::state {

// Poll until EVSEProcessing=Finished, then store the EVSE limits, publish dc_evse_present_limits and
// ev_power_ready and advance to CableCheck.
struct ChargeParameterDiscovery : public StateBase {
    ChargeParameterDiscovery(Context& ctx) : StateBase(ctx, StateID::ChargeParameterDiscovery) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::ev::din::state
