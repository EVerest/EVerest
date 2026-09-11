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

    MacAddress evse_mac{};

    MacAddress ev_mac{};
    RunId run_id{};

    int captured_sounds{0};
    int captured_aags[defs::AAG_LIST_LEN]{};

    int num_retries{0};
};

/// A session is identified by the EV's MAC and the run id together, so a second CM_SLAC_PARM.REQ
/// carrying both restarts it in place. Symmetric: neither side is the incumbent.
bool matches_identity(SessionData const& lhs, SessionData const& rhs);

/// Does this message belong to this session? A check needing no session lives in
/// protocol/validation.hpp instead.
bool validate_message(SessionData const& data, messages::cm_atten_char_rsp const& msg);
bool validate_message(SessionData const& data, messages::cm_slac_match_req const& msg);
bool validate_message(SessionData const& data, messages::cm_atten_profile_ind const& msg);
bool validate_message(SessionData const& data, messages::cm_start_atten_char_ind const& msg);

messages::cm_slac_parm_cnf make_slac_parm_cnf(SessionData const& data);
messages::cm_atten_char_ind make_atten_char_ind(SessionData const& data, int atten_offset);
} // namespace everest::slac::evse
