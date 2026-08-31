// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <chrono>

#include <everest/slac/ev/detail/actions.hpp>
#include <everest/slac/ev/detail/guards.hpp>
#include <everest/slac/ev/state/failed.hpp>
#include <everest/slac/ev/state/reset.hpp>
#include <everest/slac/ev/state/sounding.hpp>
#include <everest/slac/ev/state/wait_parm_cnf.hpp>
#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/messages.hpp>

namespace everest::slac::ev::state {

namespace {

/// Record the EVSE we are matching with, and announce that matching has begun.
void capture_slac_parm_cnf(Context& ctx, messages::HomeplugMessage const& frame) {

    auto const msg = frame.payload_as<messages::cm_slac_parm_cnf>();
    if (not msg.has_value()) {
        ctx.log_warn("Received CM_SLAC_PARM.CNF with invalid payload");
        return;
    }
    auto const* src_mac = frame.get_src_mac();
    if (src_mac == nullptr) {
        ctx.log_warn("Received CM_SLAC_PARM.CNF without source MAC");
        return;
    }
    std::copy_n(src_mac, ctx.active_session.evse_mac.size(), ctx.active_session.evse_mac.begin());
    ctx.d3_state = D3State::Matching;
}

} // namespace

void WaitParmCnf::enter() {
    auto const configured = m_ctx.slac_config.parm_req_timeout_ms;
    auto const timeout_ms = (configured > 0) ? static_cast<std::uint32_t>(configured)
                                             : static_cast<std::uint32_t>(defs::TT_MATCH_RESPONSE_MS);
    m_deadline.arm(m_ctx.current_time, std::chrono::milliseconds(timeout_ms));
}

Result WaitParmCnf::feed(SlacEvent const& ev) {
    if (std::get_if<event::Update>(&ev)) {
        if (not m_deadline.expired(m_ctx.current_time)) {
            return {};
        }
        // give up before retrying: the attempt budget is checked first
        if (not m_ctx.has_parm_req_attempts_left()) {
            return m_ctx.create_state<Failed>();
        }
        send_slac_parm_req(m_ctx);
        m_deadline.rearm(m_ctx.current_time);
        return handled();
    }

    if (auto const* frame = as_frame(ev)) {
        if (not is_slac_parm_cnf(*frame, m_ctx.active_session.run_id)) {
            return {};
        }
        // the capture can fail on a frame without a source MAC; the transition happens either way,
        // matching the boost::msm behaviour where a failed action does not veto the transition
        capture_slac_parm_cnf(m_ctx, *frame);
        return m_ctx.create_state<Sounding>();
    }

    if (std::get_if<event::Reset>(&ev)) {
        m_ctx.clear_session();
        return m_ctx.create_state<Reset>();
    }

    return {};
}

} // namespace everest::slac::ev::state
