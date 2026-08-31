// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/evse/state/matched.hpp>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>

#include <everest/slac/evse/state/failed.hpp>
#include <everest/slac/evse/state/reset.hpp>

namespace everest::slac::evse::state {

void Matched::enter() {
    auto const& cfg = m_ctx.slac_config;

    {
        std::ostringstream ss;
        ss << "Entered Matched state (EV " << format_mac_addr(m_ctx.status.ev_mac) << ", avg. attenuation "
           << std::fixed << std::setprecision(1) << m_ctx.status.average_attenuation << " dB)";
        m_ctx.log_info(ss.str());
    }
    m_ctx.clear_match_confirm_cache();
    m_ctx.signal_dlink_ready(true);

    m_mode = cfg.link_status.do_detect ? link_check_mode_for(m_ctx.modem_vendor) : LinkCheckMode::None;
    m_poll.arm(m_ctx.current_time, std::chrono::milliseconds(cfg.link_status.poll_in_matched_state_ms));

    m_consecutive_neg = 0;
    m_neg_threshold = std::max(1, cfg.link_status.debounce_count);

    m_ctx.status.match_state = SlacState::Matched;
    m_ctx.status.d3_state = D3State::Matched;
    m_ctx.status.modem_link_ready = true;

    if (m_mode != LinkCheckMode::None) {
        send_link_status_req(m_ctx, m_mode);
    }

    // Off by default. CmAmpMap_002 through 004.
    m_amp_map_awaiting_cnf = false;
    m_amp_map_retries = 0;
    if (cfg.initiate_amp_map and cfg.amp_map_len > 0) {
        if (not m_ctx.send_amp_map_req(m_ctx.status.ev_mac, cfg.amp_map_len, cfg.amp_map_data)) {
            m_ctx.log_warn("Failed to send CM_AMP_MAP.REQ");
        }
        m_amp_map_awaiting_cnf = true;
        m_amp_map_timer.arm(m_ctx.current_time, std::chrono::milliseconds(defs::TT_MATCH_RESPONSE_MS));
    }
}

bool Matched::is_amp_map_req(messages::HomeplugMessage const& frame) {
    if (not frame.is_valid() or frame.get_mmtype() != defs::MMTYPE_CM_AMP_MAP_REQ) {
        return false;
    }
    auto const req = frame.payload_as<messages::cm_amp_map_req>();
    return req.has_value() and req->am_len != 0;
}

void Matched::retransmit_amp_map() {
    if (m_amp_map_retries >= defs::C_EV_MATCH_RETRY) {
        m_amp_map_awaiting_cnf = false; // retry budget spent, stop asking
        return;
    }
    m_amp_map_retries++;
    auto const& cfg = m_ctx.slac_config;
    if (not m_ctx.send_amp_map_req(m_ctx.status.ev_mac, cfg.amp_map_len, cfg.amp_map_data)) {
        m_ctx.log_warn("Failed to resend CM_AMP_MAP.REQ");
    }
    m_amp_map_timer.rearm(m_ctx.current_time);
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
        if (m_amp_map_awaiting_cnf and m_amp_map_timer.expired(m_ctx.current_time)) {
            retransmit_amp_map();
        }
        if (m_mode != LinkCheckMode::None and m_poll.expired(m_ctx.current_time)) {
            send_link_status_req(m_ctx, m_mode);
            m_poll.rearm(m_ctx.current_time);
        }
        return handled();
    }

    if (auto const* frame = as_frame(ev)) {
        // Answered whatever the supervision mode is. Applying the reduction is the modem's job.
        if (is_amp_map_req(*frame)) {
            messages::cm_amp_map_cnf reply{};
            reply.result = defs::CM_AMP_MAP_CNF_RESULT_SUCCESS;
            if (not m_ctx.send_slac_message(frame->get_src_mac(), reply)) {
                m_ctx.log_warn("Failed to send CM_AMP_MAP.CNF");
            }
            return handled();
        }
        // Any other result leaves the retransmission running: V2G3-A09-114, CmAmpMap_004.
        if (m_amp_map_awaiting_cnf and frame->is_valid() and frame->get_mmtype() == defs::MMTYPE_CM_AMP_MAP_CNF) {
            auto const cnf = frame->payload_as<messages::cm_amp_map_cnf>();
            if (cnf.has_value() and cnf->result == defs::CM_AMP_MAP_CNF_RESULT_SUCCESS) {
                m_amp_map_awaiting_cnf = false;
                return handled();
            }
        }
        if (m_mode != LinkCheckMode::None and is_link_down(*frame, m_mode)) {
            ++m_consecutive_neg;
            if (m_consecutive_neg < m_neg_threshold) {
                m_ctx.log_warn("Negative LINK_STATUS while matched (" + std::to_string(m_consecutive_neg) + "/" +
                               std::to_string(m_neg_threshold) + "), keeping the link up while debouncing");
                return handled();
            }
            m_ctx.log_error("Connection lost in matched state");
            m_ctx.signal_error_routine_request();
            // Reset rather than Failed: start a fresh attempt instead of parking in a terminal state.
            return m_ctx.create_state<Reset>();
        }
        if (m_mode != LinkCheckMode::None and is_link_up(*frame, m_mode) and m_consecutive_neg != 0) {
            m_consecutive_neg = 0;
            m_ctx.log_info("Positive LINK_STATUS, link recovered; debounce count cleared");
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
