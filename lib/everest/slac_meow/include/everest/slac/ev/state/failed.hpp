// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/ev/states.hpp>

namespace everest::slac::ev::state {

/// Matching failed. Only a reset gets out of here.
struct Failed : public StateBase {
    explicit Failed(Context& ctx) : StateBase(ctx, StateID::Failed) {
    }

    void enter() final;
    Result feed(SlacEvent const&) final;
};

} // namespace everest::slac::ev::state
