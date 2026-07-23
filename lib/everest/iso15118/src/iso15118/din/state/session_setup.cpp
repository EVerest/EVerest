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
    // [V2G-DC-872]: session id must be cryptographically random and non-zero.
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
    m_ctx.log.enter_state("SessionSetup");
}

Result SessionSetup::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::SessionSetupRequest>()) {
        const auto evcc_id = to_mac_string(req->evcc_id);
        logf_info("Received DIN session setup with evccid: %s", evcc_id.c_str());
        m_ctx.feedback.evcc_id(evcc_id);

        // [V2G-DC-872]: the SECC keeps no session store (no pause/resume in DIN), so every SessionSetupReq
        // starts a new session. Always assign a freshly generated id (non-zero, and not the received one)
        // and answer OK_NewSessionEstablished, rather than adopting an arbitrary non-zero id from the EV.
        const auto session_id = generate_session_id();
        m_ctx.set_session_id(session_id);

        const auto res = handle_request(*req, session_id, m_ctx.session_config.evse_id, /*new_session=*/true);
        m_ctx.respond(res);

        return m_ctx.create_state<ServiceDiscovery>();
    }

    m_ctx.log("expected SessionSetupReq! But code type id: %d", variant->get_type());
    // [V2G-DC-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
    respond_sequence_error(m_ctx, *variant);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
