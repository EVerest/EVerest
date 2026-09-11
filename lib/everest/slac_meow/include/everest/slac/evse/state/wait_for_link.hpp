// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/evse/detail/link_status.hpp>
#include <everest/slac/evse/states.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::evse::state {

// Matching is agreed; waiting for the modems to form the data link. A repeated CM_SLAC_MATCH.REQ is
// answered from the cached confirmation, since the EV may not have seen the first one.
struct WaitForLink : public StateBase {
    explicit WaitForLink(Context& ctx) : StateBase(ctx, StateID::WaitForLink) {
    }

    void enter() final;
    Result feed(SlacEvent const&) final;

private:
    /// True if it was ours to answer.
    bool resend_cached_match_cnf(messages::HomeplugMessage const& frame);

    LinkCheckMode m_mode{LinkCheckMode::None};
    Timer m_poll{};
    Timer m_deadline{};
};

} // namespace everest::slac::evse::state
