// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/ev/states.hpp>

namespace everest::slac::ev::state {

/// Matched: the data link is up and the NMK is set on the modem.
struct Matched : public StateBase {
    explicit Matched(Context& ctx) : StateBase(ctx, StateID::Matched) {
    }

    void enter() final;
    Result feed(SlacEvent const&) final;
};

} // namespace everest::slac::ev::state
