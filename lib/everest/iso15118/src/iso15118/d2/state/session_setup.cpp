// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <iomanip>
#include <sstream>

#include <iso15118/d2/state/session_setup.hpp>
#include <iso15118/message/d2/session_setup.hpp>

#include <iso15118/d2/state/service_discovery.hpp>

#include <iso15118/detail/d2/context_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

namespace dt = msg::data_types;

namespace {
std::string session_id_to_string(const dt::SESSION_ID& session_id) {
    std::stringstream ss;
    ss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(session_id[0]);
    for (unsigned int i = 1; i < session_id.size(); ++i) {
        ss << ", 0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(session_id.at(i));
    }
    return ss.str();
}
bool session_is_zero(const dt::SESSION_ID& session_id) {
    return std::all_of(session_id.begin(), session_id.end(), [](int i) { return i == 0; });
}

msg::SessionSetupResponse handle_request([[maybe_unused]] const msg::SessionSetupRequest& req,
                                         const d2::Session& session, const std::string& evse_id, bool new_session) {

    msg::SessionSetupResponse res;
    setup_header(res.header, session);

    res.evse_id = evse_id;

    if (new_session) {
        return response_with_code(res, dt::ResponseCode::OK_NewSessionEstablished);
    }
    return response_with_code(res, dt::ResponseCode::OK_OldSessionJoined);
}

} // namespace

void SessionSetup::enter() {
    // m_ctx.log.enter_state("SessionSetup");
}

Result SessionSetup::feed(Event ev) {

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<msg::SessionSetupRequest>()) {

        // logf_info("Received session setup with evccid: %s", evcc_id_to_string(req->evcc_id).c_str());

        if (not session_is_zero(req->header.session_id)) {
            logf_info("Ev wants to resume old session with session_id: %s. Resuming is currently not supported!",
                      session_id_to_string(req->header.session_id).c_str());
        }

        m_ctx.session = Session();
        logf_info("New session created with session_id: %s", session_id_to_string(m_ctx.session.get_id()).c_str());

        evse_id = m_ctx.session_config.evse_id;

        const auto res = handle_request(*req, m_ctx.session, evse_id, true);

        m_ctx.respond(res);

        return m_ctx.create_state<ServiceDiscovery>();
    }
    // m_ctx.log("expected SessionSetupReq! But code type id: %d", variant->get_type());

    // Sequence Error
    const msg::Type req_type = variant->get_type();
    send_sequence_error(req_type, m_ctx);

    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::state
