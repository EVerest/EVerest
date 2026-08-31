// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>

#include <everest/slac/protocol/messages.hpp>
#include <everest/slac/protocol/types.hpp>

namespace everest::slac::evse {

/// One matching session's identity and the sounding results gathered for it.
struct SessionData;

/// "Session (run_id=..., ev_mac=...): ", so concurrent sessions stay tellable apart in the log.
std::string session_log_prefix(SessionData const& data);

struct SessionData {
    SessionData() = default;
    SessionData(MacAddress ev_mac, RunId run_id, MacAddress evse_mac);
    SessionData(const uint8_t* ev_mac, const uint8_t* run_id, const uint8_t* evse_mac);

    bool matches_identity(MacAddress const& other_ev_mac, RunId const& other_run_id) const;
    bool matches_identity(const uint8_t* other_ev_mac, const uint8_t* other_run_id) const;

    MacAddress evse_mac{};

    MacAddress ev_mac{};
    RunId run_id{};

    int captured_sounds{0};
    int captured_aags[defs::AAG_LIST_LEN]{};

    int num_retries{0};

    bool validate_message(messages::cm_atten_char_rsp const& msg) const;
    bool validate_message(messages::cm_slac_match_req const& msg) const;
    bool validate_message(messages::cm_atten_profile_ind const& msg) const;
    static bool validate_message(messages::cm_slac_parm_req const& msg);
    bool validate_message(messages::cm_start_atten_char_ind const& msg) const;

    messages::cm_slac_parm_cnf create_cm_slac_parm_cnf();
    messages::cm_atten_char_ind create_cm_atten_char_ind(int atten_offset);
    // Note (aw): this function doesn't return by value in order to optimize for fewer copies
    void create_cm_slac_match_cnf(messages::cm_slac_match_cnf& match_cnf, messages::cm_slac_match_req const& match_req,
                                  Nmk const& session_nmk);
};
} // namespace everest::slac::evse
