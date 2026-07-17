// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/session_setup.hpp>

#include <algorithm>
#include <random>

#include <iso15118/d2/state/service_discovery.hpp>

#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/session_setup.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

namespace {

bool session_is_zero(const dt::SessionId& session_id) {
    return std::all_of(session_id.begin(), session_id.end(), [](uint8_t byte) { return byte == 0; });
}

dt::SessionId generate_session_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    dt::SessionId id{};
    for (auto& byte : id) {
        byte = static_cast<uint8_t>(dist(gen));
    }
    return id;
}

} // namespace

message_2::SessionSetupResponse handle_request([[maybe_unused]] const message_2::SessionSetupRequest& req,
                                               const dt::SessionId& session_id, const std::string& evse_id,
                                               bool new_session) {
    message_2::SessionSetupResponse res;
    res.header.session_id = session_id;
    res.evse_id = evse_id;
    res.response_code =
        new_session ? dt::ResponseCode::OK_NewSessionEstablished : dt::ResponseCode::OK_OldSessionJoined;
    return res;
}

void SessionSetup::enter() {
    m_ctx.log.enter_state("SessionSetup");
}

Result SessionSetup::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    const auto req = variant->get_if<message_2::SessionSetupRequest>();
    if (req == nullptr) {
        m_ctx.log("expected SessionSetupReq! But code type id: %d", variant->get_type());
        // [V2G2-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
        respond_sequence_error(m_ctx, *variant);
        m_ctx.session_stopped = true;
        return {};
    }

    const auto& received_id = req->header.session_id;

    bool new_session = true;
    if (m_ctx.pause_ctx.has_value() and not session_is_zero(received_id) and
        received_id == m_ctx.pause_ctx->old_session_id) {
        // A returning EV re-joins the paused session with its retained id [V2G2-463].
        m_ctx.set_session_id(received_id);
        new_session = false;
        logf_info("ISO2 SessionSetup: old session joined");
    } else {
        m_ctx.set_session_id(generate_session_id());
        logf_info("ISO2 SessionSetup: new session established");
    }

    const auto res = handle_request(*req, m_ctx.get_session_id(), m_ctx.session_config.evse_id, new_session);
    m_ctx.respond(res);

    return m_ctx.create_state<ServiceDiscovery>();
}

} // namespace iso15118::d2::state
