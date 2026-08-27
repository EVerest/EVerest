// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::ev::state {

// DC WeldingDetection loop. Sends WeldingDetectionReq for a fixed number of cycles, then transitions
// to SessionStop. Guarded by the welding-detection timeout.
struct WeldingDetection : public StateBase {
    WeldingDetection(Context& ctx) : StateBase(ctx, StateID::WeldingDetection) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool first_request{true};
    bool ongoing_timeout_reached{false};
    int cycles{0};

    void send(Event ev);
};

} // namespace iso15118::d2::ev::state
