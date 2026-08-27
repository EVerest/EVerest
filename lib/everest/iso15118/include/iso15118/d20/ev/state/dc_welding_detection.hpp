// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d20::ev::state {

// Ongoing polling loop: send ev_processing=Ongoing for a few cycles (Josev parity: 3), then Finished.
// On completion, transition to SessionStop with the pending stop reason (Terminate/Pause).
struct DC_WeldingDetection : public StateBase {
    DC_WeldingDetection(Context& ctx) : StateBase(ctx, StateID::DC_WeldingDetection) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    static constexpr int WELDING_DETECTION_CYCLES = 3;

    bool first_request{true};
    int cycles{0};
    bool finish{false};
    bool sent_finished{false};
    bool ongoing_timeout_reached{false};

    void send(Event ev);
};

} // namespace iso15118::d20::ev::state
