// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/evse/state/idle.hpp>
#include <everest/slac/evse/state/matching.hpp>
#include <everest/slac/evse/state/reset.hpp>

namespace everest::slac::evse::state {

void Idle::enter() {
    m_ctx.log_info("Entered Idle state");
    m_ctx.clear_match_confirm_cache();
    m_ctx.status.match_state = SlacState::Idle;
    m_ctx.status.d3_state = D3State::Unmatched;
    m_ctx.status.modem_PIB = true;
}

Result Idle::feed(SlacEvent const& ev) {
    if (std::get_if<event::EnterBcd>(&ev)) {
        return m_ctx.create_state<Matching>();
    }

    if (std::get_if<event::Reset>(&ev)) {
        return m_ctx.create_state<Reset>();
    }

    return {};
}

} // namespace everest::slac::evse::state
