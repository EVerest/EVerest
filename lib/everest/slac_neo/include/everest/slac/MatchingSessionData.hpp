// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/slac_messages.hpp>

namespace everest::lib::slac::fsm::evse {

struct MatchingSessionData {
    MatchingSessionData() = default;
    MatchingSessionData(const uint8_t* ev_mac, const uint8_t* run_id, const uint8_t* evse_mac);

    // context related
    uint8_t evse_mac[ETH_ALEN];

    // common session related
    uint8_t ev_mac[ETH_ALEN];
    uint8_t run_id[defs::RUN_ID_LEN];

    // sounding related
    int captured_sounds{0};
    int captured_aags[defs::AAG_LIST_LEN];
    bool received_mnbc_sound{false};

    int num_retries{0};

    bool validate_message(messages::cm_atten_char_rsp const& msg) const;
    bool validate_message(messages::cm_slac_match_req const& msg) const;
    bool validate_message(messages::cm_atten_profile_ind const& msg) const;
    static bool validate_message(messages::cm_slac_parm_req const& msg);
    static bool validate_message(messages::cm_start_atten_char_ind const& msg);
    static bool validate_message(messages::cm_mnbc_sound_ind const& msg);

    messages::cm_slac_parm_cnf create_cm_slac_parm_cnf();
    messages::cm_atten_char_ind create_cm_atten_char_ind(int atten_offset);
    // Note (aw): this function doesn't return by value in order to optimize for fewer copies
    void create_cm_slac_match_cnf(messages::cm_slac_match_cnf& match_cnf, messages::cm_slac_match_req const& match_req,
                                  uint8_t const* session_nmk);
    static messages::cm_set_key_req create_cm_set_key_req(uint8_t const* session_nmk);
};
} // namespace everest::lib::slac::fsm::evse
