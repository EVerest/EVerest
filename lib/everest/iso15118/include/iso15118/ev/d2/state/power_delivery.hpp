// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>

namespace iso15118::ev::d2::state {

// Shared AC + DC state. Start -> DC CurrentDemand / AC ChargingStatus; Stop -> DC WeldingDetection /
// AC SessionStop; Renegotiate -> ChargeParameterDiscovery, which keeps the session alive.
struct PowerDelivery : public StateBase {
    enum class Phase {
        Start,
        Stop,
        Renegotiate,
    };

    PowerDelivery(Context& ctx, Phase phase) : StateBase(ctx, StateID::PowerDelivery), m_phase(phase) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    Phase m_phase;
};

} // namespace iso15118::ev::d2::state
