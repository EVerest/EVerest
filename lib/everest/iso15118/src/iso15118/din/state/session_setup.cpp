// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/session_setup.hpp>

#include <algorithm>
#include <cstdio>
#include <string>

#include <iso15118/din/state/service_discovery.hpp>

#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/session_setup.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/random.hpp>

namespace iso15118::din::state {

namespace {

bool session_is_zero(const dt::SessionId& session_id) {
    return std::all_of(session_id.begin(), session_id.end(), [](uint8_t b) { return b == 0; });
}

dt::SessionId generate_session_id() {
    // [V2G2-DC-993]: a SessionSetupReq carrying SessionID 0 gets a new, not-stored, non-zero id. (The
    // draft prints this one and 872 below with a "V2G2-DC-" prefix where its neighbours use "V2G-DC-".)
    dt::SessionId id{};
    do {
        fill_random(id.data(), id.size());
    } while (session_is_zero(id));
    return id;
}

std::string to_mac_string(const dt::EvccId& bytes) {
    std::string out;
    out.reserve(bytes.size() * 3);
    for (size_t i = 0; i < bytes.size(); ++i) {
        char buf[4];
        std::snprintf(buf, sizeof(buf), i == 0 ? "%02X" : ":%02X", bytes[i]);
        out += buf;
    }
    return out;
}

} // namespace

message_din::SessionSetupResponse handle_request(const message_din::SessionSetupRequest&,
                                                 const dt::SessionId& session_id, const std::vector<uint8_t>& evse_id,
                                                 bool new_session) {
    message_din::SessionSetupResponse res;
    setup_header(res.header, session_id);
    res.evse_id = evse_id;
    // [V2G-DC-393] OK_NewSessionEstablished for a fresh session, OK_OldSessionJoined for a re-join.
    return response_with_code(res, new_session ? dt::ResponseCode::OK_NewSessionEstablished
                                               : dt::ResponseCode::OK_OldSessionJoined);
}

void SessionSetup::enter() {
    logf_debug("Enter state: SessionSetup");
}

Result SessionSetup::on_request(const message_din::Variant& received) {
    if (const auto req = received.get_if<message_din::SessionSetupRequest>()) {
        const auto evcc_id = to_mac_string(req->evcc_id);
        logf_info("Received DIN session setup with evccid: %s", evcc_id.c_str());
        m_ctx.feedback.evcc_id(evcc_id);

        // [V2G2-DC-872]: the SECC keeps no session store (no pause/resume in DIN), so every SessionSetupReq
        // starts a new session with a freshly generated id rather than adopting the EV's non-zero one.
        const auto session_id = generate_session_id();
        m_ctx.set_session_id(session_id);

        const auto res = handle_request(*req, session_id, m_ctx.session_config.evse_id, /*new_session=*/true);
        m_ctx.respond(res);

        return m_ctx.create_state<ServiceDiscovery>();
    }

    logf_warning("Expected SessionSetupReq! But code type id: %d", received.get_type());
    respond_sequence_error(m_ctx, received);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
