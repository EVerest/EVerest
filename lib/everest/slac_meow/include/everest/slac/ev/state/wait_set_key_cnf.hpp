// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/ev/states.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::ev::state {

/// The NMK from the EVSE has been handed to the local modem; waiting for it to confirm.
struct WaitSetKeyCnf : public StateBase {
    explicit WaitSetKeyCnf(Context& ctx) : StateBase(ctx, StateID::WaitSetKeyCnf) {
    }

    void enter() final;
    Result feed(SlacEvent const&) final;

private:
    Timer m_deadline;
};

} // namespace everest::slac::ev::state
