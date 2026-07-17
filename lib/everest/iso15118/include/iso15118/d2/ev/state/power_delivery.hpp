// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::ev::state {

// Shared AC + DC state.
//   Start       -> DC: CurrentDemand, AC: ChargingStatus
//   Stop        -> DC: WeldingDetection, AC: SessionStop
//   Renegotiate -> ChargeParameterDiscovery (schedule renegotiation; session continues)
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

} // namespace iso15118::d2::ev::state
