// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::state {

struct ChargingStatus : public StateBase {
    ChargingStatus(Context& ctx) : StateBase(ctx, StateID::ChargingStatus) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool first_response{true};
};

} // namespace iso15118::d2::state
