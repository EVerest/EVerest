// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/evse/state/reset.hpp>

#include <chrono>

#include <everest/slac/evse/session_data.hpp>
#include <everest/slac/evse/state/failed.hpp>
#include <everest/slac/evse/state/idle.hpp>
#include <everest/slac/evse/state/reset_chip.hpp>
#include <everest/slac/protocol/builders.hpp>
#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/format.hpp>
#include <everest/slac/protocol/messages.hpp>

namespace everest::slac::evse::state {

bool Reset::is_retry_confirmed() const {
    return m_ctx.slac_config.set_key_handling_mode == SetKeyHandlingMode::retry_confirmed;
}

void Reset::enter() {

    if (m_ctx.slac_config.regenerate_key_on_reset) {
        if (m_ctx.slac_config.set_key_handling_mode == SetKeyHandlingMode::retry_confirmed) {
            // stage the new key; it only becomes the session key once the modem confirms
            m_ctx.slac_config.generate_nmk(m_pending_nmk);
        } else {
            m_ctx.slac_config.generate_nmk(m_ctx.slac_config.session_nmk);
        }
    } else {
        m_pending_nmk = m_ctx.slac_config.session_nmk;
    }
    if (m_ctx.slac_config.set_key_handling_mode == SetKeyHandlingMode::legacy_single_attempt) {
        m_pending_nmk = m_ctx.slac_config.session_nmk;
    }

    m_set_key_attempts = 1;
    m_settled = false;

    m_ctx.clear_match_confirm_cache();
    m_ctx.status.match_state = SlacState::Reset;
    m_ctx.status.d3_state = D3State::Unmatched;
    m_ctx.status.modem_NMK = false;

    send_set_key_req();
}

void Reset::send_set_key_req() {

    Nmk const& nmk = is_retry_confirmed() ? m_pending_nmk : m_ctx.slac_config.session_nmk;
    m_ctx.log_info("Using SLAC session NMK " + format_nmk(nmk));

    auto msg = protocol::make_set_key_req(nmk);
    m_set_key_timer.arm(m_ctx.current_time, std::chrono::milliseconds(m_ctx.slac_config.set_key_timeout_ms));

    if (not m_ctx.send_slac_message(m_ctx.slac_config.plc_peer_mac, msg)) {
        m_ctx.log_warn("Failed to send CM_SET_KEY.REQ");
    }
}

Result Reset::leave_reset() {
    if (m_ctx.slac_config.chip_reset.enabled) {
        return m_ctx.create_state<ResetChip>();
    }
    return m_ctx.create_state<Idle>();
}

Result Reset::handle_set_key_cnf(messages::HomeplugMessage const& frame) {

    auto const reply = frame.payload_as<messages::cm_set_key_cnf>();
    if (not reply.has_value()) {
        // a truncated confirmation is neither a success nor a failure
        return {};
    }

    if (accepts_set_key_cnf_success_result(m_ctx.slac_config.set_key_cnf_success_mode, reply->result)) {
        m_ctx.slac_config.session_nmk = m_pending_nmk;
        m_ctx.log_info("CM_SET_KEY.CNF success, NMK set on modem");
        m_ctx.status.modem_NMK = true;
        m_settled = true;
        return handled();
    }

    // a failure result only means anything in retry_confirmed mode
    if (not is_retry_confirmed()) {
        return {};
    }

    if (m_set_key_attempts >= m_ctx.slac_config.set_key_max_attempts) {
        m_ctx.log_error("CM_SET_KEY.CNF indicates failure with result=" + std::to_string(reply->result) +
                        " after maximum attempts; continuing to reset/idle path");
        m_settled = true;
        return handled();
    }

    // the resend itself comes from the pending timeout, not from here
    m_ctx.log_warn("CM_SET_KEY.CNF indicates failure with result=" + std::to_string(reply->result) + " on attempt " +
                   std::to_string(m_set_key_attempts) + "; retrying after timeout (max " +
                   std::to_string(m_ctx.slac_config.set_key_max_attempts) + ")");
    return handled();
}

Result Reset::feed(SlacEvent const& ev) {

    if (std::get_if<event::Update>(&ev)) {
        if (m_settled) {
            return leave_reset();
        }
        if (not m_set_key_timer.expired(m_ctx.current_time)) {
            return {};
        }

        // legacy_single_attempt has no retry, so a timeout is terminal
        if (not is_retry_confirmed()) {
            return m_ctx.create_state<Failed>();
        }

        if (m_set_key_attempts >= m_ctx.slac_config.set_key_max_attempts) {
            m_ctx.log_error("CM_SET_KEY timeout without valid CM_SET_KEY.CNF after " +
                            std::to_string(m_ctx.slac_config.set_key_max_attempts) +
                            " attempts; continuing to reset/idle path");
            m_settled = true;
            return handled();
        }

        m_set_key_attempts++;
        m_ctx.log_warn("Retrying CM_SET_KEY.REQ due to timeout. Attempt " + std::to_string(m_set_key_attempts) +
                       " of " + std::to_string(m_ctx.slac_config.set_key_max_attempts));
        send_set_key_req();
        return handled();
    }

    if (auto const* frame = as_frame(ev)) {
        if (frame->is_valid() and frame->get_mmtype() == defs::MMTYPE_CM_SET_KEY_CNF) {
            return handle_set_key_cnf(*frame);
        }
        return {};
    }

    if (std::get_if<event::Reset>(&ev)) {
        // full re-entry: a fresh key and a fresh request
        return m_ctx.create_state<Reset>();
    }

    return {};
}

void Reset::describe(StateTree& out) const {
    StateBase::describe(out);
    StateTree child;
    child.name = m_settled ? "MsgValid" : "MsgSent";
    out.children.push_back(std::move(child));
}

void Reset::signature(std::string& out) const {
    StateBase::signature(out);
    out += m_settled ? "v;" : "s;";
}

} // namespace everest::slac::evse::state
