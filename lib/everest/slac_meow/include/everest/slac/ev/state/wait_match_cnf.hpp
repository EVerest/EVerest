// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/ev/states.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::ev::state {

/// Waiting for CM_SLAC_MATCH.CNF, re-sending the request until the attempt budget runs out.
struct WaitMatchCnf : public StateBase {
    explicit WaitMatchCnf(Context& ctx) : StateBase(ctx, StateID::WaitMatchCnf) {
    }

    void enter() final;
    Result feed(SlacEvent const&) final;

private:
    Timer m_deadline;
};

} // namespace everest::slac::ev::state
