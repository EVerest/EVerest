// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <chrono>

#include <everest/slac/ev/detail/actions.hpp>
#include <everest/slac/ev/detail/guards.hpp>
#include <everest/slac/ev/state/failed.hpp>
#include <everest/slac/ev/state/reset.hpp>
#include <everest/slac/ev/state/wait_match_cnf.hpp>
#include <everest/slac/ev/state/wait_set_key_cnf.hpp>
#include <everest/slac/evse/session_data.hpp>
#include <everest/slac/protocol/builders.hpp>
#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/messages.hpp>

namespace everest::slac::ev::state {

namespace {

/// Take the NMK out of a CM_SLAC_MATCH.CNF and hand it to the local PLC modem.
void store_match_cnf_and_send_set_key(Context& ctx, messages::HomeplugMessage const& frame) {

    auto const match_cnf = frame.payload_as<messages::cm_slac_match_cnf>();
    if (not match_cnf.has_value()) {
        ctx.log_warn("Received CM_SLAC_MATCH.CNF with invalid payload");
        return;
    }

    std::copy_n(std::begin(match_cnf->nmk), defs::NMK_LEN, ctx.pending_nmk.begin());
    if (not ctx.send_slac_message(Context::EV_PLC_MAC, protocol::make_set_key_req(ctx.pending_nmk))) {
        ctx.log_warn("Failed to send CM_SET_KEY.REQ");
    }
}

} // namespace

void WaitMatchCnf::enter() {
    auto const configured = m_ctx.slac_config.match_req_timeout_ms;
    auto const timeout_ms = (configured > 0) ? static_cast<std::uint32_t>(configured)
                                             : static_cast<std::uint32_t>(defs::TT_MATCH_RESPONSE_MS);
    m_deadline.arm(m_ctx.current_time, std::chrono::milliseconds(timeout_ms));
}

Result WaitMatchCnf::feed(SlacEvent const& ev) {
    if (std::get_if<event::Update>(&ev)) {
        if (not m_deadline.expired(m_ctx.current_time)) {
            return {};
        }
        if (not m_ctx.has_match_req_attempts_left()) {
            return m_ctx.create_state<Failed>();
        }
        send_slac_match_req(m_ctx);
        m_deadline.rearm(m_ctx.current_time);
        return handled();
    }

    if (auto const* frame = as_frame(ev)) {
        if (not is_slac_match_cnf(*frame, m_ctx.active_session.run_id, m_ctx.active_session.evse_mac)) {
            return {};
        }
        store_match_cnf_and_send_set_key(m_ctx, *frame);
        return m_ctx.create_state<WaitSetKeyCnf>();
    }

    if (std::get_if<event::Reset>(&ev)) {
        m_ctx.clear_session();
        return m_ctx.create_state<Reset>();
    }

    return {};
}

} // namespace everest::slac::ev::state
