// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#include "matching_handle_slac.hpp"

#include <cstring>
#include <iomanip>
#include <sstream>

#include "../misc.hpp"
#include "everest/slac/MatchingSessionData.hpp"

namespace everest::lib::slac::fsm::evse {

void session_log(Context& ctx, MatchingSession& session, const LogLevel level, const std::string& text) {
    const auto run_id = format_run_id(session.session_data.run_id);
    const auto mac = format_mac_addr(session.session_data.ev_mac);
    std::stringstream ss;

    ss << "Session (run_id=" << run_id << ", ev_mac=" << mac << "): " << text;

    switch (level) {
    case LogLevel::DEBUG:
        ctx.log_debug(ss.str());
        break;
    case LogLevel::INFO:
        ctx.log_info(ss.str());
        break;
    case LogLevel::WARN:
        ctx.log_warn(ss.str());
        break;
    case LogLevel::ERROR:
        ctx.log_error(ss.str());
        break;
    }
}

//
// MatchingSession related
//
static MatchingSession* find_session(std::vector<MatchingSession>& sessions, const uint8_t* ev_mac,
                                     const uint8_t* run_id) {
    for (auto& session : sessions) {
        if (session.is_identified_by(ev_mac, run_id)) {
            return &session;
        }
    }

    return nullptr;
}

void MatchingState::handle_slac_message(slac::messages::HomeplugMessage& msg) {
    const auto mmtype = msg.get_mmtype();
    tmp_ev_mac = msg.get_src_mac();

    switch (mmtype) {
    case (slac::defs::MMTYPE_CM_SLAC_PARAM | slac::defs::MMTYPE_MODE_REQ):
        handle_cm_slac_parm_req(msg.get_payload<slac::messages::cm_slac_parm_req>());
        break;
    case (slac::defs::MMTYPE_CM_START_ATTEN_CHAR | slac::defs::MMTYPE_MODE_IND):
        handle_cm_start_atten_char_ind(msg.get_payload<slac::messages::cm_start_atten_char_ind>());
        break;
    case (slac::defs::MMTYPE_CM_MNBC_SOUND | slac::defs::MMTYPE_MODE_IND):
        handle_cm_mnbc_sound_ind(msg.get_payload<slac::messages::cm_mnbc_sound_ind>());
        break;
    case (slac::defs::MMTYPE_CM_ATTEN_PROFILE | slac::defs::MMTYPE_MODE_IND):
        handle_cm_atten_profile_ind(msg.get_payload<slac::messages::cm_atten_profile_ind>());
        break;
    case (slac::defs::MMTYPE_CM_ATTEN_CHAR | slac::defs::MMTYPE_MODE_RSP):
        handle_cm_atten_char_rsp(msg.get_payload<slac::messages::cm_atten_char_rsp>());
        break;
    case (slac::defs::MMTYPE_CM_SLAC_MATCH | slac::defs::MMTYPE_MODE_REQ):
        handle_cm_slac_match_req(msg.get_payload<slac::messages::cm_slac_match_req>());
        break;
    case (slac::defs::MMTYPE_CM_VALIDATE | slac::defs::MMTYPE_MODE_REQ):
        handle_cm_validate_req(msg.get_payload<slac::messages::cm_validate_req>());
        break;
    default:
        ctx.log_warn("Received non-expected SLAC message of type " + format_mmtype(mmtype));
    }
}

void MatchingState::handle_cm_slac_parm_req(const slac::messages::cm_slac_parm_req& msg) {

    if (not MatchingSessionData::validate_message(msg)) {
        ctx.log_warn("Invalid CM_SLAC_PARM.REQ received, ignoring");
        return;
    }

    // set this flag to true, to disable the retry timeout
    seen_slac_parm_req = true;

    auto session = find_session(sessions, tmp_ev_mac, msg.run_id);
    if (session) {
        // the matching session existed already, according to [V2G3-A09-16] we should restart
        *session = MatchingSession(tmp_ev_mac, msg.run_id, ctx.evse_mac);
    } else {
        // the session didn't exist, lets create it
        sessions.emplace_back(MatchingSession{tmp_ev_mac, msg.run_id, ctx.evse_mac});
        session = &sessions.back();
    }

    session_log(ctx, *session, LogLevel::INFO, "initialized, waiting for CM_START_ATTEN_CHAR_IND");

    // timeout until we need to get cm_start_atten_char_ind
    session->set_next_timeout(slac::defs::TT_MATCH_SEQUENCE_MS);

    auto param_confirm = session->session_data.create_cm_slac_parm_cnf();

    ctx.send_slac_message(param_confirm.forwarding_sta, param_confirm);
    ctx.signal_cm_slac_parm_req(tmp_ev_mac);
}

void MatchingState::handle_cm_start_atten_char_ind(const slac::messages::cm_start_atten_char_ind& msg) {

    if (not MatchingSessionData::validate_message(msg)) {
        ctx.log_warn("Invalid CM_START_ATTEN_CHAR_IND received, ignoring");
        return;
    }

    auto session = find_session(sessions, tmp_ev_mac, msg.run_id);
    if (!session) {
        ctx.log_warn("No session found for CM_START_ATTEN_CHAR_IND");
        return;
    }

    if (session->state != MatchingSubState::WAIT_FOR_START_ATTEN_CHAR) {
        if (session->state != MatchingSubState::SOUNDING)
            session_log(ctx, *session, LogLevel::WARN,
                        "needs to be in state WAIT_FOR_START_ATTEN_CHAR for CM_START_ATTEN_CHAR_IND");
        return;
    }

    // go to sounding
    session_log(ctx, *session, LogLevel::INFO, "received CM_START_ATTEN_CHAR_IND, going to substate SOUNDING");
    session->state = MatchingSubState::SOUNDING;
    session->set_next_timeout(slac::defs::TT_EVSE_MATCH_MNBC_MS);
}

void MatchingState::handle_cm_mnbc_sound_ind(const slac::messages::cm_mnbc_sound_ind& msg) {

    if (not MatchingSessionData::validate_message(msg)) {
        ctx.log_warn("Invalid CM_MNBC_SOUND_IND received, ignoring");
        return;
    }

    auto session = find_session(sessions, tmp_ev_mac, msg.run_id);
    if (!session) {
        ctx.log_warn("No session found for CM_MNBC_SOUND_IND");
        return;
    }

    if (session->state != MatchingSubState::SOUNDING) {
        session_log(ctx, *session, LogLevel::WARN, "needs to be in state SOUNDING for CM_MNBC_SOUND_IND");
        return;
    }

    session_log(ctx, *session, LogLevel::INFO, "received CM_MNBC_SOUND_IND");

    session->session_data.received_mnbc_sound = true;
}

void MatchingState::handle_cm_atten_profile_ind(const slac::messages::cm_atten_profile_ind& msg) {
    // cm_atten_profile_ind does not carry a run_id, so we can't exactly identify the session
    // FIXME (aw): for now, we only take the first one found
    MatchingSession* session = nullptr;

    for (auto& session_i : sessions) {
        if (memcmp(msg.pev_mac, session_i.session_data.ev_mac, sizeof(msg.pev_mac)) == 0) {
            session = &session_i;
        }
    }

    if (!session) {
        ctx.log_warn("No session found for CM_ATTEN_PROFILE_IND");
        return;
    }

    if (session->state != MatchingSubState::SOUNDING) {
        session_log(ctx, *session, LogLevel::WARN, "needs to be in state SOUNDING for CM_ATTEN_PROFILE_IND");
        return;
    }

    if (msg.num_groups != slac::defs::AAG_LIST_LEN) {
        session_log(ctx, *session, LogLevel::WARN, "mismatch in number of AAG groups");
        return;
    }

    session_log(ctx, *session, LogLevel::INFO, "received CM_ATTEN_PROFILE_IND");

    for (int i = 0; i < slac::defs::AAG_LIST_LEN; ++i) {
        session->session_data.captured_aags[i] += msg.aag[i];
    }

    session->session_data.captured_sounds++;

    if (session->session_data.captured_sounds < slac::defs::CM_SLAC_PARM_CNF_NUM_SOUNDS) {
        return;
    }

    // fall-through: all sounds captured
    session_log(ctx, *session, LogLevel::INFO, "received all sounds, going to substate FINALIZE_SOUNDING");
    session->state = MatchingSubState::FINALIZE_SOUNDING;
    session->set_next_timeout(FINALIZE_SOUNDING_DELAY_MS);
}

void MatchingState::handle_cm_atten_char_rsp(const slac::messages::cm_atten_char_rsp& msg) {

    auto session = find_session(sessions, tmp_ev_mac, msg.run_id);
    if (!session) {
        ctx.log_warn("No session found for CM_ATTEN_CHAR_RSP");
        return;
    }

    if (not session->session_data.validate_message(msg)) {
        session_log(ctx, *session, LogLevel::WARN, "Invalid CM_ATTEN_CHAR_RSP received, ignoring");
        return;
    }

    if (session->state != MatchingSubState::WAIT_FOR_ATTEN_CHAR_RSP) {
        session_log(ctx, *session, LogLevel::WARN,
                    "needs to be in state WAIT_FOR_ATTEN_CHAR_RSP for CM_ATTEN_CHAR_RSP");
        return;
    }

    session_log(ctx, *session, LogLevel::INFO, "received CM_ATTEN_CHAR_RSP, going to substate WAIT_FOR_SLAC_MATCH");
    session->state = MatchingSubState::WAIT_FOR_SLAC_MATCH;

    // FIXME (aw): referring to the standard, it is not clear here, if we should offset from TT_EVSE_MATCH_MNBC
    session->set_next_timeout(slac::defs::TT_EVSE_MATCH_SESSION_MS);
}

void MatchingState::handle_cm_validate_req(const slac::messages::cm_validate_req& msg) {
    // NOTE: CM_VALIDATE.REQ does not specify its session
    // EVSE allowed to not implement: [V2G3-A09-51]
    ctx.log_warn("Received CM_VALIDATE.REQ / not implemented - will return failure code");

    slac::messages::cm_validate_cnf validate_cnf;
    validate_cnf.signal_type = slac::defs::CM_VALIDATE_REQ_SIGNAL_TYPE;
    validate_cnf.toggle_num = 0;
    validate_cnf.result = slac::defs::CM_VALIDATE_REQ_RESULT_FAILURE;

    ctx.send_slac_message(tmp_ev_mac, validate_cnf);
}

void MatchingState::handle_cm_slac_match_req(const slac::messages::cm_slac_match_req& msg) {

    auto session = find_session(sessions, tmp_ev_mac, msg.run_id);
    if (!session) {
        ctx.log_warn("No session found for CM_SLAC_MATCH_REQ");
        return;
    }

    if (not session->session_data.validate_message(msg)) {
        session_log(ctx, *session, LogLevel::WARN, "Invalid CM_SLAC_MATCH_REQ received, ignoring");
        return;
    }

    if (session->state != MatchingSubState::WAIT_FOR_SLAC_MATCH && session->state != MatchingSubState::MATCH_COMPLETE) {
        session_log(ctx, *session, LogLevel::WARN,
                    "needs to be in state WAIT_FOR_SLAC_MATCH or MATCH_COMPLETE for CM_SLAC_MATCH_REQ");
        return;
    }

    session_log(ctx, *session, LogLevel::INFO,
                "Received CM_SLAC_MATCH_REQ, sending CM_SLAC_MATCH_CNF -> session complete");

    static constexpr uint8_t wrong_session_nmk[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                                      0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    auto const* session_nmk = ctx.slac_config.session_nmk;

    if (ctx.slac_config.link_status.debug_simulate_failed_matching) {
        ctx.log_info("Sending wrong NMK to EV to simulate a failed link setup after match request");
        session_nmk = wrong_session_nmk;
    }

    match_cnf_message = std::make_unique<slac::messages::cm_slac_match_cnf>();
    session->session_data.create_cm_slac_match_cnf(*match_cnf_message, msg, session_nmk);

    ctx.send_slac_message(tmp_ev_mac, *match_cnf_message);

    session->state = MatchingSubState::MATCH_COMPLETE;

    // call this immediately again in MatchedState::callback to handle things
    session->set_next_timeout(0);
    ctx.signal_cm_slac_match_cnf(tmp_ev_mac);
}

void MatchingState::finalize_sounding(MatchingSession& session) {
    session_log(ctx, session, LogLevel::INFO, "Finalize sounding, sending CM_ATTEN_CHAR_IND");
    session.state = MatchingSubState::WAIT_FOR_ATTEN_CHAR_RSP;

    auto atten_char = session.session_data.create_cm_atten_char_ind(ctx.slac_config.sounding_atten_adjustment);

    ctx.send_slac_message(session.session_data.ev_mac, atten_char);

    session.set_next_timeout(slac::defs::TT_MATCH_RESPONSE_MS);

    int aag_overall_sum = 0;
    for (size_t i = 0; i < slac::defs::AAG_LIST_LEN; ++i) {
        aag_overall_sum += atten_char.attenuation_profile.aag[i];
    }
    std::ostringstream ss;
    ss << "Avg atten.: " << std::fixed << std::setprecision(1)
       << (static_cast<double>(aag_overall_sum) / slac::defs::AAG_LIST_LEN) << " dB";
    if (ctx.slac_config.sounding_atten_adjustment != 0) {
        ss << " plus offset " << std::to_string(ctx.slac_config.sounding_atten_adjustment) << " dB";
    }
    ss << ", from " << std::to_string(slac::defs::AAG_LIST_LEN) << " groups, " << session.session_data.captured_sounds
       << " sounds";
    session_log(ctx, session, LogLevel::INFO, ss.str());
}

} // namespace everest::lib::slac::fsm::evse
