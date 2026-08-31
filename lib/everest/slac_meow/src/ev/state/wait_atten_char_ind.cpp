// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <chrono>

#include <everest/slac/ev/detail/actions.hpp>
#include <everest/slac/ev/detail/guards.hpp>
#include <everest/slac/ev/state/failed.hpp>
#include <everest/slac/ev/state/reset.hpp>
#include <everest/slac/ev/state/wait_atten_char_ind.hpp>
#include <everest/slac/ev/state/wait_match_cnf.hpp>
#include <everest/slac/protocol/defs.hpp>

namespace everest::slac::ev::state {

void WaitAttenCharInd::enter() {
    m_deadline.arm(m_ctx.current_time, std::chrono::milliseconds(defs::TT_EV_ATTEN_RESULTS_MS));
}

Result WaitAttenCharInd::feed(SlacEvent const& ev) {
    if (std::get_if<event::Update>(&ev)) {
        if (m_deadline.expired(m_ctx.current_time)) {
            return m_ctx.create_state<Failed>();
        }
        return {};
    }

    if (auto const* frame = as_frame(ev)) {
        if (m_ctx.all_sounding_messages_sent() and is_atten_char_ind_for_run(*frame, m_ctx.active_session.run_id)) {
            send_atten_char_rsp_and_match_req(m_ctx, *frame);
            return m_ctx.create_state<WaitMatchCnf>();
        }
        return {};
    }

    if (std::get_if<event::Reset>(&ev)) {
        m_ctx.clear_session();
        return m_ctx.create_state<Reset>();
    }

    return {};
}

} // namespace everest::slac::ev::state
