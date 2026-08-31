// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/evse/state/matching.hpp>

#include <algorithm>
#include <chrono>

#include <everest/slac/evse/state/failed.hpp>
#include <everest/slac/evse/state/idle.hpp>
#include <everest/slac/evse/state/matched.hpp>
#include <everest/slac/evse/state/reset.hpp>
#include <everest/slac/evse/state/wait_for_link.hpp>
#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/messages.hpp>

namespace everest::slac::evse::state {

namespace {

bool is_slac_parm_req(messages::HomeplugMessage const& frame) {
    return frame.is_valid() and frame.get_mmtype() == defs::MMTYPE_CM_SLAC_PARAM_REQ;
}

bool is_validate_req(messages::HomeplugMessage const& frame) {
    return frame.is_valid() and frame.get_mmtype() == defs::MMTYPE_CM_VALIDATE_REQ;
}

} // namespace

void Matching::enter() {
    m_deadline.arm(m_ctx.current_time, std::chrono::milliseconds(m_ctx.slac_config.slac_init_timeout_ms));
    m_failed_matching_reset_once = false;
    m_validate_armed = false;
    m_validate_step2_pending = false;
    m_validate_step1_retries = 0;
    m_validate_owner_mac = MacAddress{};
    m_ctx.validation_done = false;
    m_ctx.log_info("Entered Matching state, waiting for CM_SLAC_PARM.REQ");
    m_ctx.status.match_state = SlacState::Matching;
    m_ctx.status.d3_state = D3State::Matching;
}

void Matching::leave() {
    m_sessions.clear();
    m_ctx.status.session_count = 0;
}

int Matching::max_matching_sessions() const {
    return std::max(1, m_ctx.slac_config.max_matching_sessions);
}

bool Matching::any_session_matched() const {
    return std::any_of(m_sessions.begin(), m_sessions.end(), [](auto const& s) { return s->matched(); });
}

bool Matching::all_sessions_failed() const {
    if (m_sessions.empty()) {
        return false;
    }
    return std::all_of(m_sessions.begin(), m_sessions.end(), [](auto const& s) { return s->failed(); });
}

Result Matching::check_outcome() {
    auto const out_of_options =
        all_sessions_failed() or (m_sessions.empty() and m_deadline.expired(m_ctx.current_time));

    if (not m_failed_matching_reset_once and m_ctx.slac_config.reset_instead_of_fail and out_of_options) {
        m_ctx.status.session_count = 0;
        m_sessions.clear();
        m_deadline.arm(m_ctx.current_time, std::chrono::milliseconds(m_ctx.slac_config.slac_init_timeout_ms));
        m_failed_matching_reset_once = true;
        return handled();
    }

    if (out_of_options and (not m_ctx.slac_config.reset_instead_of_fail or m_failed_matching_reset_once)) {
        return m_ctx.create_state<Failed>();
    }

    if (any_session_matched()) {
        if (m_ctx.slac_config.link_status.do_detect) {
            return m_ctx.create_state<WaitForLink>();
        }
        return m_ctx.create_state<Matched>();
    }

    return {};
}

void Matching::add_session(messages::HomeplugMessage const& frame) {

    auto const msg = frame.payload_as<messages::cm_slac_parm_req>();
    if (not msg.has_value()) {
        return;
    }
    if (not SessionData::validate_message(*msg)) {
        return;
    }
    // The confirmation is out but this state is only left on the next tick; in that window a fresh
    // request must still get no answer. PLCLinkStatus_003.
    if (any_session_matched()) {
        m_ctx.log_info("Ignoring CM_SLAC_PARM.REQ, match already completed");
        return;
    }

    auto const ev_mac = byte_array_from_wire<MacAddress>(frame.get_src_mac());
    auto const run_id = byte_array_from_wire<RunId>(msg->run_id);
    SessionData data(ev_mac, run_id, m_ctx.evse_mac);

    auto existing = std::find_if(m_sessions.begin(), m_sessions.end(), [&data](auto const& session) {
        return session->data().matches_identity(data.ev_mac, data.run_id);
    });

    if (existing == m_sessions.end()) {
        auto const session_limit = max_matching_sessions();
        if (static_cast<int>(m_sessions.size()) >= session_limit) {
            m_ctx.log_warn("Ignoring CM_SLAC_PARM.REQ because max_matching_sessions was reached (" +
                           std::to_string(session_limit) + ")");
            return;
        }
        m_sessions.emplace_back(std::make_unique<Session>(m_ctx, data));
    } else {
        (*existing)->restart(data);
    }

    m_ctx.log_info(session_log_prefix(data) + "Received CM_SLAC_PARM.REQ, sending CM_SLAC_PARM.CNF");
    auto param_confirm = data.create_cm_slac_parm_cnf();
    if (not m_ctx.send_slac_message(data.ev_mac, param_confirm)) {
        m_ctx.log_warn("Failed to send CM_SLAC_PARM.CNF");
    }
    m_ctx.signal_cm_slac_parm_req(data.ev_mac.data());
    m_ctx.status.session_count = static_cast<int>(m_sessions.size());
}

void Matching::send_validate_cnf_reply(MacAddress const& mac, std::uint8_t result, std::uint8_t toggle_num) {
    messages::cm_validate_cnf reply{};
    reply.signal_type = defs::CM_VALIDATE_REQ_SIGNAL_TYPE;
    reply.toggle_num = toggle_num;
    reply.result = result;

    if (not m_ctx.send_slac_message(mac, reply)) {
        m_ctx.log_warn("Failed to send CM_VALIDATE.CNF");
    }
}

void Matching::handle_validate_req(messages::HomeplugMessage const& frame) {
    auto const* src_mac = frame.get_src_mac();
    auto const req = frame.payload_as<messages::cm_validate_req>();

    // Ignored outright, leaving any step-1 repetition running: CmValidate_004.
    if (not req.has_value() or req->signal_type != defs::CM_VALIDATE_REQ_SIGNAL_TYPE or src_mac == nullptr) {
        return;
    }
    auto const sender = byte_array_from_wire<MacAddress>(src_mac);

    if (req->timer == 0x00) {
        // Step 1. Another EV asking mid-validation is told we are busy: V2G3-M09-13, CmValidate_009.
        if (m_validate_armed and sender != m_validate_owner_mac) {
            send_validate_cnf_reply(sender, defs::CM_VALIDATE_REQ_RESULT_NOT_READY, 0);
            return;
        }
        // A repeated step 1 resets the retry counter: CmValidate_002.
        m_validate_owner_mac = sender;
        m_validate_armed = true;
        m_validate_step2_pending = false;
        m_validate_step1_retries = 0;
        // Snapshot the counter here, at step 1: the EV only toggles during the step-2 window, so
        // every edge from now to the end of that window is a toggle. Snapshotting at step 2 would
        // race the pilot path against the slower SLAC frame path and miss the first toggle.
        m_validate_baseline_bc = m_ctx.bc_transition_count;
        m_validate_timer.arm(m_ctx.current_time, std::chrono::milliseconds(defs::TT_MATCH_SEQUENCE_MS));
        send_validate_cnf_reply(m_validate_owner_mac, defs::CM_VALIDATE_REQ_RESULT_READY, 0);
        return;
    }

    if (not m_validate_armed or sender != m_validate_owner_mac) {
        return;
    }
    // The EV terminating or skipping: no CNF, validation over. CmValidate_005 through 008.
    if (req->result != defs::CM_VALIDATE_REQ_RESULT_READY) {
        m_validate_armed = false;
        m_validate_step2_pending = false;
        return;
    }
    // validate_tick sends the CNF when the window elapses.
    m_validate_step2_pending = true;
    m_validate_timer.arm(m_ctx.current_time, std::chrono::milliseconds((static_cast<long long>(req->timer) + 1) * 100));
}

void Matching::validate_tick() {
    if (not(m_validate_armed or m_validate_step2_pending) or not m_validate_timer.expired(m_ctx.current_time)) {
        return;
    }

    if (m_validate_step2_pending) {
        // one B/C edge is counted per BCB toggle, so the delta since the baseline is the count
        auto const seen = m_ctx.bc_transition_count - m_validate_baseline_bc;
        auto const toggles = static_cast<std::uint8_t>(std::max(0, std::min(seen, 255)));
        send_validate_cnf_reply(m_validate_owner_mac, defs::CM_VALIDATE_REQ_RESULT_SUCCESS, toggles);
        m_validate_armed = false;
        m_validate_step2_pending = false;
        m_ctx.log_info("CM_VALIDATE: detected " + std::to_string(toggles) + " BCB toggle(s) during the toggle window");
        // CmSlacMatch_003/004, cmValidate variant.
        m_ctx.validation_done = true;
        m_ctx.validation_match_window.arm(m_ctx.current_time, std::chrono::milliseconds(defs::TT_MATCH_SEQUENCE_MS));
        return;
    }

    if (m_validate_step1_retries < defs::C_EV_MATCH_RETRY) {
        m_validate_step1_retries++;
        send_validate_cnf_reply(m_validate_owner_mac, defs::CM_VALIDATE_REQ_RESULT_READY, 0);
        m_validate_timer.rearm(m_ctx.current_time);
        return;
    }
    // Retry limit reached with no step 2: stop answering. CmValidate_003/004.
    m_validate_armed = false;
}

Result Matching::feed(SlacEvent const& ev) {
    if (std::get_if<event::Update>(&ev)) {
        validate_tick();
        // decided before the sessions see this tick
        auto outcome = check_outcome();
        if (outcome.new_state != nullptr) {
            return outcome;
        }
        for (auto& session : m_sessions) {
            session->feed(ev);
        }
        return handled();
    }

    if (auto const* frame = as_frame(ev)) {
        if (is_slac_parm_req(*frame)) {
            add_session(*frame);
            return handled();
        }
        if (is_validate_req(*frame)) {
            // answered here and deliberately not forwarded to the sessions
            handle_validate_req(*frame);
            return handled();
        }
        for (auto& session : m_sessions) {
            session->feed(ev);
        }
        return handled();
    }

    if (std::get_if<event::Reset>(&ev)) {
        return m_ctx.create_state<Reset>();
    }

    if (std::get_if<event::LeaveBcd>(&ev)) {
        return m_ctx.create_state<Idle>();
    }

    return {};
}

void Matching::signature(std::string& out) const {
    StateBase::signature(out);
    out += 'n';
    out += std::to_string(m_sessions.size());
    out += ';';
    for (auto const& session : m_sessions) {
        session->signature(out);
    }
}

void Matching::describe(StateTree& out) const {
    StateBase::describe(out);
    out.sessions.reserve(m_sessions.size());
    for (auto const& session : m_sessions) {
        StateTree entry;
        session->describe(entry);
        out.sessions.push_back(std::move(entry));
    }
}

} // namespace everest::slac::evse::state
