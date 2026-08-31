// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/ev/states.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::ev::state {

/// Emitting the sounding burst: 3 CM_START_ATTEN_CHAR.IND then 10 CM_MNBC_SOUND.IND, one per tick.
///
/// The deadline covers the whole burst, so sending a sounding message must NOT re-arm it. In the
/// boost::msm version that was the difference between an internal and an external self transition.
struct Sounding : public StateBase {
    explicit Sounding(Context& ctx) : StateBase(ctx, StateID::Sounding) {
    }

    void enter() final;
    Result feed(SlacEvent const&) final;

private:
    Timer m_deadline;
};

} // namespace everest::slac::ev::state
