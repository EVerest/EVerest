// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include "everest/slac/slac_messages.hpp"
#include "everest/slac/slac_utils.hpp"
#include <cstring>
#include <everest/slac/MatchingSessionData.hpp>

namespace everest::lib::slac::fsm::evse {
MatchingSessionData::MatchingSessionData(const uint8_t* ev_mac, const uint8_t* run_id, const uint8_t* evse_mac) {
    std::memcpy(this->ev_mac, ev_mac, sizeof(this->ev_mac));
    std::memcpy(this->run_id, run_id, sizeof(this->run_id));
    std::memcpy(this->evse_mac, evse_mac, sizeof(this->evse_mac));
    std::memset(captured_aags, 0, sizeof(captured_aags));
}

bool MatchingSessionData::validate_message(messages::cm_atten_char_rsp const& msg) const {
    if (msg.application_type not_eq slac::defs::COMMON_APPLICATION_TYPE) {
        return false;
    }
    if (msg.security_type not_eq slac::defs::COMMON_SECURITY_TYPE) {
        return false;
    }
    std::uint8_t source_id_ref[slac::messages::SOURCE_ID_LEN];
    std::memset(source_id_ref, 0, sizeof(source_id_ref));
    if (std::memcmp(source_id_ref, msg.source_id, slac::messages::SOURCE_ID_LEN)) {
        return false;
    }
    std::uint8_t resp_id_ref[slac::messages::RESP_ID_LEN];
    std::memset(resp_id_ref, 0, sizeof(resp_id_ref));
    if (std::memcmp(resp_id_ref, msg.resp_id, slac::messages::RESP_ID_LEN)) {
        return false;
    }
    if (msg.result not_eq slac::defs::CM_ATTEN_CHAR_RSP_RESULT) {
        return false;
    }

    if (std::memcmp(msg.source_address, ev_mac, ETH_ALEN)) {
        return false;
    }
    return true;
}

bool MatchingSessionData::validate_message(messages::cm_slac_match_req const& msg) const {
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
    std::uint8_t pev_id_ref[slac::messages::PEV_ID_LEN];
    std::memset(pev_id_ref, 0, sizeof(pev_id_ref));
    if (memcmp(pev_id_ref, msg.pev_id, slac::messages::PEV_ID_LEN)) {
        return false;
    }
    // EVSE ID = 0x00 TC_SECC_CMN_VTB_CmSlacMatch_017/018(?)
    std::uint8_t evse_id_ref[slac::messages::EVSE_ID_LEN];
    std::memset(evse_id_ref, 0, sizeof(evse_id_ref));
    if (std::memcmp(evse_id_ref, msg.evse_id, slac::messages::EVSE_ID_LEN)) {
        return false;
    }

    // PEV MAC TC_SECC_CMN_VTB_CmSlacMatch_015/016(?)
    if (std::memcmp(msg.pev_mac, ev_mac, ETH_ALEN)) {
        return false;
    }
    // EVSE MAC TC_SECC_CMN_VTB_CmSlacMatch_019/020
    if (std::memcmp(evse_mac, msg.evse_mac, ETH_ALEN)) {
        return false;
    }
    // RunID TC_SECC_CMN_VTB_CmSlacMatch_021/022
    if (std::memcmp(msg.run_id, run_id, slac::defs::RUN_ID_LEN)) {
        return false;
    }
    return true;
}

bool MatchingSessionData::validate_message(messages::cm_atten_profile_ind const& msg) const {
    if (std::memcmp(msg.pev_mac, ev_mac, sizeof(msg.pev_mac))) {
        return false;
    }
    if (msg.num_groups != slac::defs::AAG_LIST_LEN) {
        return false;
    }
    return true;
}

bool MatchingSessionData::validate_message(messages::cm_slac_parm_req const& msg) {
    if (msg.application_type not_eq slac::defs::COMMON_APPLICATION_TYPE) {
        return false;
    }
    if (msg.security_type not_eq slac::defs::COMMON_SECURITY_TYPE) {
        return false;
    }

    return true;
}

bool MatchingSessionData::validate_message(messages::cm_start_atten_char_ind const& msg) {
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

    return true;
}

bool MatchingSessionData::validate_message(messages::cm_mnbc_sound_ind const& msg) {
    if (msg.application_type not_eq slac::defs::COMMON_APPLICATION_TYPE) {
        return false;
    }
    if (msg.security_type not_eq slac::defs::COMMON_SECURITY_TYPE) {
        return false;
    }

    return true;
}

messages::cm_slac_parm_cnf MatchingSessionData::create_cm_slac_parm_cnf() {
    messages::cm_slac_parm_cnf param_confirm;

    std::memcpy(param_confirm.m_sound_target, slac::defs::BROADCAST_MAC_ADDRESS,
                sizeof(slac::defs::BROADCAST_MAC_ADDRESS));
    param_confirm.num_sounds = slac::defs::CM_SLAC_PARM_CNF_NUM_SOUNDS;
    param_confirm.timeout = slac::defs::CM_SLAC_PARM_CNF_TIMEOUT;
    param_confirm.resp_type = slac::defs::CM_SLAC_PARM_CNF_RESP_TYPE;
    std::memcpy(param_confirm.forwarding_sta, ev_mac, sizeof(param_confirm.forwarding_sta));
    param_confirm.application_type = slac::defs::COMMON_APPLICATION_TYPE;
    param_confirm.security_type = slac::defs::COMMON_SECURITY_TYPE;
    std::memcpy(param_confirm.run_id, run_id, sizeof(param_confirm.run_id));

    return param_confirm;
}

messages::cm_atten_char_ind MatchingSessionData::create_cm_atten_char_ind(int atten_offset) {
    messages::cm_atten_char_ind atten_char_ind;

    atten_char_ind.application_type = slac::defs::COMMON_APPLICATION_TYPE;
    atten_char_ind.security_type = slac::defs::COMMON_SECURITY_TYPE;
    std::memcpy(atten_char_ind.source_address, ev_mac, sizeof(atten_char_ind.source_address));
    std::memcpy(atten_char_ind.run_id, run_id, sizeof(atten_char_ind.run_id));
    // memcpy(atten_char_ind.source_id, session_ev_id, sizeof(atten_char_ind.source_id));
    std::memset(atten_char_ind.source_id, 0, sizeof(atten_char_ind.source_id));
    // memcpy(atten_char_ind.resp_id, sample_evse_vin, sizeof(atten_char_ind.resp_id));
    std::memset(atten_char_ind.resp_id, 0, sizeof(atten_char_ind.resp_id));
    atten_char_ind.num_sounds = captured_sounds;
    atten_char_ind.attenuation_profile.num_groups = slac::defs::AAG_LIST_LEN;
    if (captured_sounds != 0) {
        for (int i = 0; i < slac::defs::AAG_LIST_LEN; ++i) {
            atten_char_ind.attenuation_profile.aag[i] = captured_aags[i] / captured_sounds + atten_offset;
        }
    } else {
        // FIXME (aw): what to do here, if we didn't receive any sounds?
        std::memset(atten_char_ind.attenuation_profile.aag, 0x01, sizeof(atten_char_ind.attenuation_profile.aag));
    }

    return atten_char_ind;
}

// Note (aw): this function doesn't return by value in order to optimize for fewer copies
void MatchingSessionData::create_cm_slac_match_cnf(messages::cm_slac_match_cnf& match_cnf,
                                                   messages::cm_slac_match_req const& match_req,
                                                   uint8_t const* session_nmk) {
    match_cnf.application_type = slac::defs::COMMON_APPLICATION_TYPE;
    match_cnf.security_type = slac::defs::COMMON_SECURITY_TYPE;
    match_cnf.mvf_length = htole16(slac::defs::CM_SLAC_MATCH_CNF_MVF_LENGTH);
    std::memcpy(match_cnf.pev_id, match_req.pev_id, sizeof(match_cnf.pev_id));
    std::memcpy(match_cnf.pev_mac, match_req.pev_mac, sizeof(match_cnf.pev_mac));
    std::memcpy(match_cnf.evse_id, match_req.evse_id, sizeof(match_cnf.evse_id));
    std::memcpy(match_cnf.evse_mac, match_req.evse_mac, sizeof(match_cnf.evse_mac));
    std::memcpy(match_cnf.run_id, match_req.run_id, sizeof(match_cnf.run_id));
    std::memset(match_cnf._rerserved, 0, 8);
    match_cnf._reserved2 = 0;
    utils::generate_nid_from_nmk(match_cnf.nid, session_nmk);
    std::memcpy(match_cnf.nmk, session_nmk, sizeof(match_cnf.nmk));
}

messages::cm_set_key_req MatchingSessionData::create_cm_set_key_req(uint8_t const* session_nmk) {
    messages::cm_set_key_req set_key_req;

    set_key_req.key_type = defs::CM_SET_KEY_REQ_KEY_TYPE_NMK;
    set_key_req.my_nonce = 0x00000000;
    set_key_req.your_nonce = 0x00000000;
    set_key_req.pid = defs::CM_SET_KEY_REQ_PID_HLE;
    set_key_req.prn = htole16(defs::CM_SET_KEY_REQ_PRN_UNUSED);
    set_key_req.pmn = defs::CM_SET_KEY_REQ_PMN_UNUSED;
    set_key_req.cco_capability = defs::CM_SET_KEY_REQ_CCO_CAP_NONE;
    utils::generate_nid_from_nmk(set_key_req.nid, session_nmk);
    set_key_req.new_eks = defs::CM_SET_KEY_REQ_PEKS_NMK_KNOWN_TO_STA;
    memcpy(set_key_req.new_key, session_nmk, sizeof(set_key_req.new_key));

    return set_key_req;
}

} // namespace everest::lib::slac::fsm::evse
