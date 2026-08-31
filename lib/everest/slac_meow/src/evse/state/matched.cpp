// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/evse/state/matched.hpp>

#include <chrono>

#include <everest/slac/evse/state/failed.hpp>
#include <everest/slac/evse/state/reset.hpp>

namespace everest::slac::evse::state {

void Matched::enter() {
    auto const& cfg = m_ctx.slac_config;

    m_ctx.clear_match_confirm_cache();
    m_ctx.signal_dlink_ready(true);

    m_mode = cfg.link_status.do_detect ? link_check_mode_for(m_ctx.modem_vendor) : LinkCheckMode::None;
    m_poll.arm(m_ctx.current_time, std::chrono::milliseconds(cfg.link_status.poll_in_matched_state_ms));

    m_ctx.status.match_state = SlacState::Matched;
    m_ctx.status.d3_state = D3State::Matched;
    m_ctx.status.modem_link_ready = true;

    if (m_mode != LinkCheckMode::None) {
        send_link_status_req(m_ctx, m_mode);
    }
}

void Matched::leave() {
    m_ctx.signal_dlink_ready(false);
    m_ctx.status.ev_mac.fill(0);
    m_ctx.status.average_attenuation = 0.f;
    m_ctx.status.modem_link_ready = false;
    m_ctx.clear_match_confirm_cache();
}

Result Matched::feed(SlacEvent const& ev) {
    if (std::get_if<event::Update>(&ev)) {
        if (m_mode != LinkCheckMode::None and m_poll.expired(m_ctx.current_time)) {
            send_link_status_req(m_ctx, m_mode);
            m_poll.rearm(m_ctx.current_time);
            return handled();
        }
        return {};
    }

    if (auto const* frame = as_frame(ev)) {
        if (m_mode != LinkCheckMode::None and is_link_down(*frame, m_mode)) {
            m_ctx.log_error("Connection lost in matched state");
            m_ctx.signal_error_routine_request();
            // Reset rather than Failed: start a fresh attempt instead of parking in a terminal state.
            return m_ctx.create_state<Reset>();
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
