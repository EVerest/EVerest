// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/ev/states.hpp>

namespace everest::slac::ev::state {

/// Waiting to be told to start matching.
struct Idle : public StateBase {
    explicit Idle(Context& ctx) : StateBase(ctx, StateID::Idle) {
    }

    void enter() final;
    Result feed(SlacEvent const&) final;
};

} // namespace everest::slac::ev::state
