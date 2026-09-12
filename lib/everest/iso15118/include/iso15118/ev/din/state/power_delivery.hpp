// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/states.hpp>

namespace iso15118::ev::din::state {

// Start -> CurrentDemand (ReadyToChargeState=true); Stop -> WeldingDetection (ReadyToChargeState=false).
struct PowerDelivery : public StateBase {
    enum class Phase {
        Start,
        Stop,
    };

    PowerDelivery(Context& ctx, Phase phase) : StateBase(ctx, StateID::PowerDelivery), m_phase(phase) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    Phase m_phase;
};

} // namespace iso15118::ev::din::state
