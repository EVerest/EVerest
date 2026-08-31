// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/evse/detail/link_status.hpp>
#include <everest/slac/evse/states.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::evse::state {

// The data link is up and HLC can run. Without link supervision this state sits here until reset
// from outside.
struct Matched : public StateBase {
    explicit Matched(Context& ctx) : StateBase(ctx, StateID::Matched) {
    }

    void enter() final;
    void leave() final;
    Result feed(SlacEvent const&) final;

private:
    LinkCheckMode m_mode{LinkCheckMode::None};
    Timer m_poll;
};

} // namespace everest::slac::evse::state
