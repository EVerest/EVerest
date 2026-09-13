// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>

namespace iso15118::ev::d2::state {

// Polls ChargeParameterDiscoveryReq until EVSEProcessing=Finished, then branches DC -> CableCheck,
// AC -> PowerDelivery(Start).
struct ChargeParameterDiscovery : public StateBase {
    ChargeParameterDiscovery(Context& ctx) : StateBase(ctx, StateID::ChargeParameterDiscovery) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    void send();
};

} // namespace iso15118::ev::d2::state
