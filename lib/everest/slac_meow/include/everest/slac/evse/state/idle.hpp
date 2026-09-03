// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/evse/states.hpp>

namespace everest::slac::evse::state {

/// Keys are set on the modem and nothing is plugged in. Waiting for the control pilot.
struct Idle : public StateBase {
    explicit Idle(Context& ctx) : StateBase(ctx, StateID::Idle) {
    }

    void enter() final;
    Result feed(SlacEvent const&) final;
};

} // namespace everest::slac::evse::state
