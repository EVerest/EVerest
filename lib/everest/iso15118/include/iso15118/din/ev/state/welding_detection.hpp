// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::din::ev::state {

// Optional welding detection: poll until the EVSE output voltage has dropped below a safe threshold,
// with a cycle-count backstop (Josev parity: 3), then advance to SessionStop. Guarded by
// V2G_SECC_SequenceTimeout.
struct WeldingDetection : public StateBase {
    WeldingDetection(Context& ctx) : StateBase(ctx, StateID::WeldingDetection) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    static constexpr int WELDING_DETECTION_CYCLES = 3;
    // Below this EVSE present voltage the DC link is considered safe and welding detection may stop.
    static constexpr float WELDING_DETECTION_SAFE_VOLTAGE_V = 60.0f;

    bool first_request{true};
    int cycles{0};
    bool ongoing_timeout_reached{false};

    void send(Event ev);
};

} // namespace iso15118::din::ev::state
