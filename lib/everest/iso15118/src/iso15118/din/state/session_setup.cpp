// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/session_setup.hpp>

#include <algorithm>
#include <cstdio>
#include <random>
#include <string>

#include <iso15118/din/state/service_discovery.hpp>

#include <iso15118/detail/din/state/session_setup.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

namespace {

bool session_is_zero(const dt::SessionId& session_id) {
    return std::all_of(session_id.begin(), session_id.end(), [](uint8_t b) { return b == 0; });
}

dt::SessionId generate_session_id() {
    std::random_device rd;
    std::uniform_int_distribution<int> dist(1, 255);
    dt::SessionId id{};
    for (auto& byte : id) {
        byte = static_cast<uint8_t>(dist(rd));
    }
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
                                                 const dt::SessionId& session_id,
                                                 const std::vector<uint8_t>& evse_id, bool new_session) {
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

        // A fresh session carries an all-zero id; a re-join carries the previously assigned id.
        const bool new_session = session_is_zero(req->header.session_id);
        const auto session_id = new_session ? generate_session_id() : req->header.session_id;
        m_ctx.set_session_id(session_id);

        const auto res = handle_request(*req, session_id, m_ctx.session_config.evse_id, new_session);
        m_ctx.respond(res);

        return m_ctx.create_state<ServiceDiscovery>();
    }

    m_ctx.log("expected SessionSetupReq! But code type id: %d", variant->get_type());
    message_din::SessionSetupResponse res;
    setup_header(res.header, m_ctx.get_session_id());
    m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED_SequenceError));
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
