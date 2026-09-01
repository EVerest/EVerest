// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/evse/states.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::evse::state {

/// Optional vendor chip reset after the NMK has been set.
struct ResetChip : public StateBase {
    explicit ResetChip(Context& ctx) : StateBase(ctx, StateID::ResetChip) {
    }

    void enter() final;
    Result feed(SlacEvent const&) final;

private:
    Timer m_delay;
    Timer m_timeout;
    bool m_request_sent{false};
};

} // namespace everest::slac::evse::state
