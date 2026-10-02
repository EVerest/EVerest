// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct DC_WeldingDetection : public StateBase {
public:
    DC_WeldingDetection(Context& ctx) : StateBase(ctx, StateID::DC_WeldingDetection) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // Set once the closing EVProcessing=Finished request has been sent; the next response
    // acknowledges it and ends the state.
    bool finished_sent{false};
};

} // namespace iso15118::ev::d20::state
