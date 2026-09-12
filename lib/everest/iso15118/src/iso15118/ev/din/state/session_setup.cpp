// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/state/session_setup.hpp>

#include <cstdio>
#include <string>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/din/context_helper.hpp>
#include <iso15118/ev/din/state/service_discovery.hpp>
#include <iso15118/message_din/session_setup.hpp>

namespace iso15118::ev::din::state {

namespace dt = message_din::datatypes;

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

void SessionSetup::enter() {
    logf_debug("Enter state: SessionSetup (DIN 70121)");
    message_din::SessionSetupRequest req{};
    const auto& mac = m_ctx.params().evcc_mac;
    req.evcc_id.assign(mac.begin(), mac.end());
    m_ctx.send_request(req);
}

Result SessionSetup::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = variant->get_if<message_din::SessionSetupResponse>();
    if (res == nullptr) {
        logf_error("expected SessionSetupRes, got message type id: %d", static_cast<int>(variant->get_type()));
        m_ctx.stop_session();
        return Result::stopping();
    }

    // DIN SECCs answer OK, OK_NewSessionEstablished or OK_OldSessionJoined.
    if (res->response_code >= dt::ResponseCode::FAILED) {
        logf_error("SessionSetupRes rejected with response_code %d", static_cast<int>(res->response_code));
        m_ctx.stop_session();
        return Result::stopping();
    }
    if (res->header.session_id == dt::SessionId{}) {
        logf_error("SessionSetupRes carries an all-zero session id");
        m_ctx.stop_session();
        return Result::stopping();
    }

    m_ctx.set_session_id(res->header.session_id);
    m_ctx.evse_info.evse_id = to_hex_string(res->evse_id);
    m_ctx.feedback.evse_id(m_ctx.evse_info.evse_id);
    logf_info("DIN 70121 session established, EVSEID %s", m_ctx.evse_info.evse_id.c_str());

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }
    return m_ctx.create_state<ServiceDiscovery>();
}

} // namespace iso15118::ev::din::state
