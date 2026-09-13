// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct DC_PreCharge : public StateBase {
public:
    DC_PreCharge(Context& ctx) : StateBase(ctx, StateID::DC_PreCharge) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    // Set once the closing EVProcessing=Finished request has been sent. The next
    // response is the SECC's acknowledgement of it rather than another voltage
    // reading, so it must not be re-checked against the tolerance.
    bool finished_sent{false};
};

} // namespace iso15118::ev::d20::state
