// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/ev/states.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::ev::state {

/// Sounding is done; waiting for the EVSE to report the attenuation profile.
struct WaitAttenCharInd : public StateBase {
    explicit WaitAttenCharInd(Context& ctx) : StateBase(ctx, StateID::WaitAttenCharInd) {
    }

    void enter() final;
    Result feed(SlacEvent const&) final;

private:
    Timer m_deadline{};
};

} // namespace everest::slac::ev::state
