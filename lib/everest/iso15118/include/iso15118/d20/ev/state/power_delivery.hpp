// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d20::ev::state {

// Shared AC + DC state. Start -> DC_ChargeLoop (AC_ChargeLoop later); Stop -> DC: DC_WeldingDetection,
// AC: SessionStop. For BPT the channel selection is CHARGE.
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

} // namespace iso15118::d20::ev::state
