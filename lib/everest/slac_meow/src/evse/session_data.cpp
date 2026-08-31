// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include "everest/slac/protocol/messages.hpp"
#include "everest/slac/protocol/utils.hpp"
#include <algorithm>
#include <everest/slac/evse/session_data.hpp>
#include <everest/slac/protocol/format.hpp>

namespace everest::slac::evse {

std::string session_log_prefix(SessionData const& data) {
    return "Session (run_id=" + format_run_id(data.run_id) + ", ev_mac=" + format_mac_addr(data.ev_mac) + "): ";
}
SessionData::SessionData(MacAddress ev_mac, RunId run_id, MacAddress evse_mac) :
    evse_mac(evse_mac), ev_mac(ev_mac), run_id(run_id) {
}

bool SessionData::matches_identity(MacAddress const& other_ev_mac, RunId const& other_run_id) const {
    return ev_mac == other_ev_mac and run_id == other_run_id;
}

bool SessionData::validate_message(messages::cm_atten_char_rsp const& msg) const {
    if (msg.application_type not_eq slac::defs::COMMON_APPLICATION_TYPE) {
        return false;
    }
    if (msg.security_type not_eq slac::defs::COMMON_SECURITY_TYPE) {
        return false;
    }
    constexpr StationId source_id_ref{};
    if (not wire_equal(msg.source_id, source_id_ref)) {
        return false;
    }
    constexpr StationId resp_id_ref{};
    if (not wire_equal(msg.resp_id, resp_id_ref)) {
        return false;
    }
    if (msg.result not_eq slac::defs::CM_ATTEN_CHAR_RSP_RESULT) {
        return false;
    }

    if (not wire_equal(msg.source_address, ev_mac)) {
        return false;
    }
    // ISO 15118-3: the CM_ATTEN_CHAR.RSP must carry this session's runID. Without the check a
    // mismatched-runID RSP was accepted and the SECC stopped retransmitting
    // (AttenuationCharacterization_008).
    if (not wire_equal(msg.run_id, run_id)) {
        return false;
    }
    return true;
}

bool SessionData::validate_message(messages::cm_slac_match_req const& msg) const {
    if (msg.application_type not_eq slac::defs::COMMON_APPLICATION_TYPE) {
        return false;
    }
    if (msg.security_type not_eq slac::defs::COMMON_SECURITY_TYPE) {
        return false;
    }
    if (msg.mvf_length not_eq slac::defs::CM_SLAC_MATCH_REQ_MVF_LENGTH) {
        return false;
    }
    // PEV ID = 0x00 TC_SECC_CMN_VTB_CmSlacMatch_013/014(?)
    constexpr StationId pev_id_ref{};
    if (not wire_equal(msg.pev_id, pev_id_ref)) {
        return false;
    }
    // EVSE ID = 0x00 TC_SECC_CMN_VTB_CmSlacMatch_017/018(?)
    constexpr StationId evse_id_ref{};
    if (not wire_equal(msg.evse_id, evse_id_ref)) {
        return false;
    }

    // PEV MAC TC_SECC_CMN_VTB_CmSlacMatch_015/016(?)
    if (not wire_equal(msg.pev_mac, ev_mac)) {
        return false;
    }
    // EVSE MAC TC_SECC_CMN_VTB_CmSlacMatch_019/020
    if (not wire_equal(msg.evse_mac, evse_mac)) {
        return false;
    }
    // RunID TC_SECC_CMN_VTB_CmSlacMatch_021/022
    if (not wire_equal(msg.run_id, run_id)) {
        return false;
    }
    return true;
}

bool SessionData::validate_message(messages::cm_atten_profile_ind const& msg) const {
    if (not wire_equal(msg.pev_mac, ev_mac)) {
        return false;
    }
    if (msg.num_groups != slac::defs::AAG_LIST_LEN) {
        return false;
    }
    return true;
}

bool SessionData::validate_message(messages::cm_start_atten_char_ind const& msg) const {
    if (msg.application_type not_eq slac::defs::COMMON_APPLICATION_TYPE) {
        return false;
    }
    if (msg.security_type not_eq slac::defs::COMMON_SECURITY_TYPE) {
        return false;
    }
    if (msg.num_sounds == 0) { // Don't be strict to the ISO 15118-3
        return false;
    }
    if (msg.timeout == 0) { // Don't be strict to the ISO 15118-3
        return false;
    }
    if (msg.resp_type not_eq slac::defs::CM_SLAC_PARM_CNF_RESP_TYPE) {
        return false;
    }
    if (not wire_equal(msg.forwarding_sta, ev_mac)) {
        return false;
    }
    if (not wire_equal(msg.run_id, run_id)) {
        return false;
    }

    return true;
}

messages::cm_slac_parm_cnf SessionData::create_cm_slac_parm_cnf() {
    messages::cm_slac_parm_cnf param_confirm{};

    copy_wire(param_confirm.m_sound_target, slac::defs::BROADCAST_MAC_ADDRESS);
    param_confirm.num_sounds = slac::defs::CM_SLAC_PARM_CNF_NUM_SOUNDS;
    param_confirm.timeout = slac::defs::CM_SLAC_PARM_CNF_TIMEOUT;
    param_confirm.resp_type = slac::defs::CM_SLAC_PARM_CNF_RESP_TYPE;
    copy_to_wire(param_confirm.forwarding_sta, ev_mac);
    param_confirm.application_type = slac::defs::COMMON_APPLICATION_TYPE;
    param_confirm.security_type = slac::defs::COMMON_SECURITY_TYPE;
    copy_to_wire(param_confirm.run_id, run_id);

    return param_confirm;
}

messages::cm_atten_char_ind SessionData::create_cm_atten_char_ind(int atten_offset) {
    messages::cm_atten_char_ind atten_char_ind{};

    atten_char_ind.application_type = slac::defs::COMMON_APPLICATION_TYPE;
    atten_char_ind.security_type = slac::defs::COMMON_SECURITY_TYPE;
    copy_to_wire(atten_char_ind.source_address, ev_mac);
    copy_to_wire(atten_char_ind.run_id, run_id);
    zero_wire(atten_char_ind.source_id);
    zero_wire(atten_char_ind.resp_id);
    atten_char_ind.num_sounds = captured_sounds;
    atten_char_ind.attenuation_profile.num_groups = slac::defs::AAG_LIST_LEN;
    if (captured_sounds != 0) {
        for (int i = 0; i < slac::defs::AAG_LIST_LEN; ++i) {
            atten_char_ind.attenuation_profile.aag[i] = captured_aags[i] / captured_sounds + atten_offset;
        }
    } else {
        // FIXME (aw): what to do here, if we didn't receive any sounds?
        std::fill_n(atten_char_ind.attenuation_profile.aag, slac::defs::AAG_LIST_LEN, std::uint8_t{0x01});
    }

    return atten_char_ind;
}

// Note (aw): this function doesn't return by value in order to optimize for fewer copies
void SessionData::create_cm_slac_match_cnf(messages::cm_slac_match_cnf& match_cnf,
                                           messages::cm_slac_match_req const& match_req, Nmk const& session_nmk) {
    match_cnf.application_type = slac::defs::COMMON_APPLICATION_TYPE;
    match_cnf.security_type = slac::defs::COMMON_SECURITY_TYPE;
    match_cnf.mvf_length = htole16(slac::defs::CM_SLAC_MATCH_CNF_MVF_LENGTH);
    copy_wire(match_cnf.pev_id, match_req.pev_id);
    copy_wire(match_cnf.pev_mac, match_req.pev_mac);
    copy_wire(match_cnf.evse_id, match_req.evse_id);
    copy_wire(match_cnf.evse_mac, match_req.evse_mac);
    copy_wire(match_cnf.run_id, match_req.run_id);
    zero_wire(match_cnf._rerserved);
    match_cnf._reserved2 = 0;
    utils::generate_nid_from_nmk(match_cnf.nid, session_nmk.data());
    copy_to_wire(match_cnf.nmk, session_nmk);
}

} // namespace everest::slac::evse
