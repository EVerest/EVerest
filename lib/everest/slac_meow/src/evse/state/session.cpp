// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/evse/state/session.hpp>

#include <chrono>

#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/messages.hpp>

namespace everest::slac::evse::state::session {

namespace {

/// The settling delay between the last sound and reporting the averaged attenuation.
constexpr std::uint32_t FINALIZE_SOUNDING_DELAY_MS = 45;

/// mmtype match, payload present, and the payload belongs to this session.
template <typename MsgT>
bool check_message(messages::HomeplugMessage const& frame, std::uint16_t expected, SessionData const& session_data) {
    if (frame.get_mmtype() != expected) {
        return false;
    }
    auto const msg = frame.payload_as<MsgT>();
    if (not msg.has_value()) {
        return false;
    }
    return session_data.validate_message(*msg);
}

bool is_start_atten_char(messages::HomeplugMessage const& frame, SessionData const& data) {
    return check_message<messages::cm_start_atten_char_ind>(frame, defs::MMTYPE_CM_START_ATTEN_CHAR_IND, data);
}

bool is_atten_profile_ind(messages::HomeplugMessage const& frame, SessionData const& data) {
    return check_message<messages::cm_atten_profile_ind>(frame, defs::MMTYPE_CM_ATTEN_PROFILE_IND, data);
}

bool is_atten_char_rsp(messages::HomeplugMessage const& frame, SessionData const& data) {
    return check_message<messages::cm_atten_char_rsp>(frame, defs::MMTYPE_CM_ATTEN_CHAR_RSP, data);
}

bool is_slac_match_req(messages::HomeplugMessage const& frame, SessionData const& data) {
    return check_message<messages::cm_slac_match_req>(frame, defs::MMTYPE_CM_SLAC_MATCH_REQ, data);
}

void send_atten_char_ind(Context& ctx, SessionData& data) {
    auto atten_char = data.create_cm_atten_char_ind(ctx.slac_config.sounding_atten_adjustment);
    if (not ctx.send_slac_message(data.ev_mac, atten_char)) {
        ctx.log_warn("Failed to send CM_ATTEN_CHAR.IND");
    }

    int aag_overall_sum = 0;
    for (std::size_t i = 0; i < defs::AAG_LIST_LEN; ++i) {
        aag_overall_sum += atten_char.attenuation_profile.aag[i];
    }
    ctx.status.average_attenuation = aag_overall_sum / defs::AAG_LIST_LEN;
}

/// Answer CM_SLAC_MATCH.REQ, cache the answer, and publish the EV MAC.
void send_match_cnf(Context& ctx, SessionData& data, messages::HomeplugMessage const& frame) {
    messages::cm_slac_match_cnf& reply = ctx.match_confirm_cache.message;

    auto const msg = frame.payload_as<messages::cm_slac_match_req>();
    if (not msg.has_value()) {
        return;
    }

    static constexpr Nmk failed_match_session_nmk{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                                  0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};

    Nmk const* session_nmk = &ctx.slac_config.session_nmk;
    if (ctx.slac_config.link_status.debug_simulate_failed_matching) {
        ctx.log_info("Sending wrong NMK to EV to simulate a failed link setup after match request");
        session_nmk = &failed_match_session_nmk;
    }

    data.create_cm_slac_match_cnf(reply, *msg, *session_nmk);
    if (not ctx.send_slac_message(data.ev_mac, reply)) {
        ctx.log_warn("Failed to send CM_SLAC_MATCH.CNF");
    }
    ctx.signal_cm_slac_match_cnf(data.ev_mac.data());
    ctx.cache_match_confirm_message(reply, data.ev_mac, data.evse_mac, data.run_id);
    std::copy(std::begin(data.ev_mac), std::end(data.ev_mac), std::begin(ctx.status.ev_mac));
}

} // namespace

void WaitStartAtten::enter() {
    m_deadline.arm(m_ctx.current_time, std::chrono::milliseconds(defs::TT_MATCH_SEQUENCE_MS));
}

Result WaitStartAtten::feed(SlacEvent const& ev) {
    if (std::get_if<event::Update>(&ev)) {
        if (m_deadline.expired(m_ctx.current_time)) {
            return m_ctx.create_state<Failed>(m_data);
        }
        return {};
    }

    if (auto const* frame = as_frame(ev)) {
        // A START that arrives in the same tick the state already timed out is too late: the
        // session fails on the next update rather than starting a sounding phase it cannot finish.
        if (not m_deadline.expired(m_ctx.current_time) and is_start_atten_char(*frame, m_data)) {
            return m_ctx.create_state<Sounding>(m_data);
        }
    }

    return {};
}

void Sounding::enter() {
    m_deadline.arm(m_ctx.current_time, std::chrono::milliseconds(defs::TT_EVSE_MATCH_MNBC_MS));
}

Result Sounding::feed(SlacEvent const& ev) {
    if (std::get_if<event::Update>(&ev)) {
        if (m_deadline.expired(m_ctx.current_time)) {
            return m_ctx.create_state<FinalizeSounding>(m_data);
        }
        return {};
    }

    if (auto const* frame = as_frame(ev)) {
        if (is_atten_profile_ind(*frame, m_data)) {
            auto const msg = frame->payload_as<messages::cm_atten_profile_ind>();
            if (msg.has_value()) {
                for (int i = 0; i < defs::AAG_LIST_LEN; ++i) {
                    m_data.captured_aags[i] += msg->aag[i];
                }
                m_data.captured_sounds++;
            }

            // V2G3-A09-44 allows leaving as soon as the sounds are in, which saves the rest of the
            // 600 ms window the EV would otherwise spend waiting for a result the EVSE already has.
            if (m_data.captured_sounds >= defs::CM_SLAC_PARM_CNF_NUM_SOUNDS) {
                return m_ctx.create_state<FinalizeSounding>(m_data);
            }
            return handled();
        }
    }

    return {};
}

void FinalizeSounding::enter() {
    m_deadline.arm(m_ctx.current_time, std::chrono::milliseconds(FINALIZE_SOUNDING_DELAY_MS));
}

Result FinalizeSounding::feed(SlacEvent const& ev) {
    if (std::get_if<event::Update>(&ev)) {
        if (m_deadline.expired(m_ctx.current_time)) {
            send_atten_char_ind(m_ctx, m_data);
            return m_ctx.create_state<WaitAttenRsp>(m_data);
        }
    }
    return {};
}

void WaitAttenRsp::enter() {
    m_deadline.arm(m_ctx.current_time, std::chrono::milliseconds(defs::TT_MATCH_RESPONSE_MS));
}

Result WaitAttenRsp::feed(SlacEvent const& ev) {
    if (std::get_if<event::Update>(&ev)) {
        if (not m_deadline.expired(m_ctx.current_time)) {
            return {};
        }
        if (m_data.num_retries >= defs::C_EV_MATCH_RETRY) {
            return m_ctx.create_state<Failed>(m_data);
        }
        send_atten_char_ind(m_ctx, m_data);
        m_data.num_retries++;
        m_deadline.rearm(m_ctx.current_time);
        return handled();
    }

    if (auto const* frame = as_frame(ev)) {
        if (is_atten_char_rsp(*frame, m_data)) {
            return m_ctx.create_state<WaitSlacMatch>(m_data);
        }
    }

    return {};
}

void WaitSlacMatch::enter() {
    m_deadline.arm(m_ctx.current_time, std::chrono::milliseconds(defs::TT_EVSE_MATCH_SESSION_MS));
}

Result WaitSlacMatch::feed(SlacEvent const& ev) {
    if (std::get_if<event::Update>(&ev)) {
        if (m_deadline.expired(m_ctx.current_time)) {
            return m_ctx.create_state<Failed>(m_data);
        }
        // A late request gets no answer: CmSlacMatch_003/004, cmValidate variant.
        if (m_ctx.validation_done and m_ctx.validation_match_window.expired(m_ctx.current_time)) {
            return m_ctx.create_state<Failed>(m_data);
        }
        return {};
    }

    if (auto const* frame = as_frame(ev)) {
        if (is_slac_match_req(*frame, m_data)) {
            send_match_cnf(m_ctx, m_data, *frame);
            return m_ctx.create_state<MatchComplete>(m_data);
        }
    }

    return {};
}

Result MatchComplete::feed(SlacEvent const&) {
    return {};
}

Result Failed::feed(SlacEvent const&) {
    return {};
}

Session::Session(Context& ctx, SessionData data) : m_ctx(ctx), m_data(std::move(data)) {
    m_data.num_retries = 0;
    m_fsm.emplace(m_ctx.create_state<WaitStartAtten>(m_data));
}

void Session::restart(SessionData data) {
    // drop the running machine before the data it references is replaced
    m_fsm.reset();
    m_data = std::move(data);
    m_data.num_retries = 0;
    m_fsm.emplace(m_ctx.create_state<WaitStartAtten>(m_data));
}

void Session::feed(SlacEvent const& ev) {
    if (m_fsm.has_value()) {
        m_fsm->feed(ev);
    }
}

bool Session::matched() const {
    return m_fsm.has_value() and m_fsm->get_current_state_id() == StateID::SessionMatchComplete;
}

bool Session::failed() const {
    return m_fsm.has_value() and m_fsm->get_current_state_id() == StateID::SessionFailed;
}

void Session::signature(std::string& out) const {
    if (m_fsm.has_value()) {
        out += std::to_string(static_cast<int>(m_fsm->get_current_state_id()));
        out += ';';
    }
}

void Session::describe(StateTree& out) const {
    if (m_fsm.has_value()) {
        out.name = to_string(m_fsm->get_current_state_id());
    }
}

} // namespace everest::slac::evse::state::session
