// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/ev/detail/actions.hpp>
#include <everest/slac/ev/state/idle.hpp>
#include <everest/slac/ev/state/reset.hpp>
#include <everest/slac/ev/state/wait_parm_cnf.hpp>

namespace everest::slac::ev::state {

void Idle::enter() {
    m_ctx.d3_state = D3State::Unmatched;
    m_ctx.signal_dlink_ready(false);
}

Result Idle::feed(SlacEvent const& ev) {
    if (std::get_if<event::TriggerMatching>(&ev)) {
        start_matching(m_ctx);
        return m_ctx.create_state<WaitParmCnf>();
    }

    if (std::get_if<event::Reset>(&ev)) {
        return m_ctx.create_state<Reset>();
    }

    return {};
}

} // namespace everest::slac::ev::state
