// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/evse/detail/link_status.hpp>
#include <everest/slac/evse/states.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::evse::state {

// The data link is up and HLC can run. Without link supervision this state sits here until reset
// from outside; with it, debounce_count consecutive negative answers are needed to declare the link
// lost, and any positive answer clears the count.
struct Matched : public StateBase {
    explicit Matched(Context& ctx) : StateBase(ctx, StateID::Matched) {
    }

    void enter() final;
    void leave() final;
    Result feed(SlacEvent const&) final;

private:
    /// am_len == 0 is invalid and is left unanswered: CmAmpMap_005.
    static bool is_amp_map_req(messages::HomeplugMessage const& frame);

    void retransmit_amp_map();

    LinkCheckMode m_mode{LinkCheckMode::None};
    Timer m_poll{};
    int m_consecutive_neg{0};
    int m_neg_threshold{1};

    // SECC-initiated CM_AMP_MAP: sent on entry, retransmitted until confirmed. CmAmpMap_003/004.
    bool m_amp_map_awaiting_cnf{false};
    int m_amp_map_retries{0};
    Timer m_amp_map_timer{};
};

} // namespace everest::slac::evse::state
