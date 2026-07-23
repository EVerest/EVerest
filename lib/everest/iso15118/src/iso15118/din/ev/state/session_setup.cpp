// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/ev/state/session_setup.hpp>

#include <cstdio>

#include <iso15118/din/ev/state/service_discovery.hpp>
#include <iso15118/din/ev/timeouts.hpp>

#include <iso15118/detail/din/ev/state/session_setup.hpp>
#include <iso15118/detail/din/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::ev::state {

namespace {

std::string to_hex_string(const dt::EvseId& bytes) {
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02X", byte);
        out += buf;
    }
    return out;
}

} // namespace

namespace session_setup {

message_din::SessionSetupRequest create_request(const std::array<uint8_t, 6>& evcc_mac,
                                                const dt::SessionId& session_id) {
    message_din::SessionSetupRequest req;
    req.header.session_id = session_id;
    req.evcc_id.assign(evcc_mac.begin(), evcc_mac.end());
    return req;
}

Result handle_response(const message_din::SessionSetupResponse& res) {
    Result result;
    result.valid = (res.response_code == dt::ResponseCode::OK_NewSessionEstablished) or
                   (res.response_code == dt::ResponseCode::OK_OldSessionJoined) or
                   (res.response_code == dt::ResponseCode::OK);
    result.new_session = (res.response_code == dt::ResponseCode::OK_NewSessionEstablished);
    result.session_id = res.header.session_id;
    result.evse_id = to_hex_string(res.evse_id);
    return result;
}

} // namespace session_setup

using namespace session_setup;

void SessionSetup::enter() {
    m_ctx.log.enter_state("SessionSetup");
}

din::ev::Result SessionSetup::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        // Carries the resumed session id (pause re-join) or an all-zero id for a fresh session.
        auto req = create_request(m_ctx.session_config.evcc_mac, m_ctx.get_session_id());
        m_ctx.send_request(req);
        m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("SessionSetup message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_din::SessionSetupResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("SessionSetup failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        m_ctx.set_session_id(result.session_id);
        m_ctx.evse_info.evse_id = result.evse_id;
        m_ctx.feedback.evse_id(result.evse_id);

        logf_info("DIN session established with evseid: %s", result.evse_id.c_str());

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }
        return m_ctx.create_state<ServiceDiscovery>();
    }

    m_ctx.log("expected SessionSetupRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::ev::state
