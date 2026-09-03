// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/evse/state/wait_for_link.hpp>

#include <chrono>

#include <everest/slac/evse/session_data.hpp>
#include <everest/slac/evse/state/failed.hpp>
#include <everest/slac/evse/state/matched.hpp>
#include <everest/slac/evse/state/reset.hpp>
#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/messages.hpp>

namespace everest::slac::evse::state {

void WaitForLink::enter() {
    auto const& cfg = m_ctx.slac_config;

    m_ctx.log_info("Waiting for Link to be ready...");

    m_mode = link_check_mode_for(m_ctx.modem_vendor);

    m_poll.arm(m_ctx.current_time, std::chrono::milliseconds(cfg.link_status.retry_ms));
    m_deadline.arm(m_ctx.current_time, std::chrono::milliseconds(cfg.link_status.timeout_ms));

    m_ctx.status.match_state = SlacState::WaitForLink;
    // still in the matching phase - the CM_SLAC_MATCH.CNF is out and the link is pending
    m_ctx.status.d3_state = D3State::Matching;

    if (m_mode != LinkCheckMode::None) {
        send_link_status_req(m_ctx, m_mode);
    }
}

bool WaitForLink::resend_cached_match_cnf(messages::HomeplugMessage const& frame) {
    auto const& cache = m_ctx.match_confirm_cache;

    if (frame.get_mmtype() != defs::mmtype::SLAC_MATCH_REQ) {
        return false;
    }
    auto const msg = frame.payload_as<messages::cm_slac_match_req>();
    if (not msg.has_value() or not cache.valid) {
        return false;
    }
    auto const* source_mac = frame.get_src_mac();
    if (source_mac == nullptr or not wire_pointer_equal(source_mac, cache.ev_mac)) {
        return false;
    }
    SessionData data(cache.ev_mac, cache.run_id, cache.evse_mac);
    if (not validate_message(data, *msg)) {
        return false;
    }

    if (not m_ctx.send_slac_message(cache.ev_mac, cache.message)) {
        m_ctx.log_warn("Failed to send cached CM_SLAC_MATCH.CNF");
    }
    return true;
}

Result WaitForLink::feed(SlacEvent const& ev) {
    // an unsupported modem cannot report a link, so there is nothing to wait for
    if (m_mode == LinkCheckMode::None) {
        if (std::get_if<event::Reset>(&ev)) {
            return m_ctx.create_state<Reset>();
        }
        return m_ctx.create_state<Failed>();
    }

    if (std::get_if<event::Update>(&ev)) {
        // the poll is checked before the overall deadline, as the sub machine was before the parent
        if (m_poll.expired(m_ctx.current_time)) {
            send_link_status_req(m_ctx, m_mode);
            m_poll.rearm(m_ctx.current_time);
            return handled();
        }
        if (m_deadline.expired(m_ctx.current_time)) {
            return m_ctx.create_state<Failed>();
        }
        return {};
    }

    if (auto const* message = get_if_message(ev)) {
        if (is_link_up(*message, m_mode)) {
            return m_ctx.create_state<Matched>();
        }
        if (resend_cached_match_cnf(*message)) {
            // this was an external self transition in boost::msm, so the poll timer restarts
            m_poll.rearm(m_ctx.current_time);
            return handled();
        }
        return {};
    }

    if (std::get_if<event::Reset>(&ev)) {
        return m_ctx.create_state<Reset>();
    }

    if (std::get_if<event::LeaveBcd>(&ev)) {
        return m_ctx.create_state<Reset>();
    }

    return {};
}

} // namespace everest::slac::evse::state
