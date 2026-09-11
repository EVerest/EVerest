// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/ev/states.hpp>

namespace everest::slac::ev::state {

/// Entry state. Announces UNMATCHED and drops straight to Idle on the next tick.
struct Reset : public StateBase {
    explicit Reset(Context& ctx) : StateBase(ctx, StateID::Reset) {
    }

    void enter() final;
    Result feed(SlacEvent const&) final;
};

} // namespace everest::slac::ev::state
