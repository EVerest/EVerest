// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/ev/detail/actions.hpp>
#include <everest/slac/ev/state/idle.hpp>
#include <everest/slac/ev/state/reset.hpp>
#include <everest/slac/ev/state/wait_parm_cnf.hpp>

namespace everest::slac::ev::state {

void Reset::enter() {
    m_ctx.d3_state = D3State::Unmatched;
    m_ctx.signal_dlink_ready(false);
    m_ctx.log_debug("EV MSM entered reset");
}

Result Reset::feed(SlacEvent const& ev) {
    if (std::get_if<event::Update>(&ev)) {
        return m_ctx.create_state<Idle>();
    }

    if (std::get_if<event::TriggerMatching>(&ev)) {
        start_matching(m_ctx);
        return m_ctx.create_state<WaitParmCnf>();
    }

    if (std::get_if<event::Reset>(&ev)) {
        // re-enter, so the UNMATCHED signal is emitted again
        return m_ctx.create_state<Reset>();
    }

    return {};
}

} // namespace everest::slac::ev::state
