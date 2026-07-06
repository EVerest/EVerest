// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH, Roger Bedell, and Contributors to EVerest
#include <algorithm>

#include <openssl/evp.h>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/authorization_setup.hpp>
#include <iso15118/ev/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/session_setup.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/io/sha_hash.hpp>
#include <iso15118/message/session_setup.hpp>

namespace iso15118::ev::d20::state {

namespace {

bool session_is_zero(const message_20::datatypes::SessionId& session_id) {
    return std::all_of(session_id.begin(), session_id.end(), [](int i) { return i == 0; });
}

io::sha512_hash_t calculate_new_cert_session_id_hash(const io::sha512_hash_t& charger_cert_hash,
                                                     const message_20::datatypes::SessionId& session_id) {
    io::sha512_hash_t session_id_charger_hash{};
    std::array<std::uint8_t, 64 + 8> concatenated_session_id_charger{};

    std::copy(session_id.begin(), session_id.end(), concatenated_session_id_charger.begin());
    std::copy(charger_cert_hash.begin(), charger_cert_hash.end(),
              concatenated_session_id_charger.begin() + session_id.size());

    unsigned int digestlen{0};

    const auto result = EVP_Digest(concatenated_session_id_charger.data(), concatenated_session_id_charger.size(),
                                   session_id_charger_hash.data(), &digestlen, EVP_sha512(), nullptr);
    if (not result) {
        logf_error("X509_digest failed");
        return std::array<std::uint8_t, 64>{};
    }

    return session_id_charger_hash;
}

} // namespace

void SessionSetup::enter() {
    m_ctx.log.enter_state("SessionSetup");

    message_20::SessionSetupRequest req{};
    setup_header(req.header, m_ctx.get_session());
    req.evccid = m_ctx.get_evcc_id();
    m_ctx.send_request(req);
}

Result SessionSetup::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    // SessionSetup establishes the session id (it arrives IN the response), so the
    // generic session-id check does not apply; validate the returned id here instead.
    const auto res = variant->get_if<message_20::SessionSetupResponse>();
    if (res == nullptr) {
        logf_error("expected SessionSetupResponse! But code type id: %d", variant->get_type());
        m_ctx.stop_session();
        return {};
    }

    if (not check_response_code(res->response_code)) {
        m_ctx.stop_session();
        return {};
    }

    if (res->evseid.size() <= 0) {
        logf_error("EVSEID is empty. Abort the session.");
        m_ctx.stop_session();
        return {};
    }

    const auto charger_cert_hash = m_ctx.get_charger_cert_hash();

    bool session_resumed = false;

    if (res->response_code == message_20::datatypes::ResponseCode::OK_NewSessionEstablished) {
        logf_info("New session established by EVSE.");

        // A new session must carry a non-zero returned id.
        if (session_is_zero(res->header.session_id)) {
            logf_error("Returned SessionID is zero although a new session was requested. Abort the session.");
            m_ctx.stop_session();
            return {};
        }

        m_ctx.get_session().set_id(res->header.session_id);

        // Save the hash of charger cert and session id for a later resume.
        if (charger_cert_hash.has_value()) {
            const auto new_charger_cert_session_hash =
                calculate_new_cert_session_id_hash(charger_cert_hash.value(), res->header.session_id);
            m_ctx.set_charger_cert_session_hash(new_charger_cert_session_hash);
        } else {
            logf_warning("No charger certificate hash available although a new session was established.");
        }
    } else if (res->response_code == message_20::datatypes::ResponseCode::OK_OldSessionJoined) {
        // On resume the returned id must match the stored (paused) one, and the pairing
        // of charger and EV must be the same as before: the saved hash of charger cert +
        // session id must match the one recomputed from the returned id.
        if (res->header.session_id != m_ctx.get_session().get_id()) {
            logf_error("Resumed SessionID does not match the stored session id. Abort the session.");
            m_ctx.stop_session();
            return {};
        }

        if (not charger_cert_hash.has_value()) {
            logf_error("No charger certificate hash available although an old session was resumed. Abort the session.");
            m_ctx.stop_session();
            return {};
        }

        const auto new_charger_cert_session_hash =
            calculate_new_cert_session_id_hash(charger_cert_hash.value(), res->header.session_id);
        if (m_ctx.get_charger_cert_session_hash() != new_charger_cert_session_hash) {
            logf_error("Charger certificate/session hash does not match the saved one although an old session was "
                       "resumed. Abort the session.");
            m_ctx.stop_session();
            return {};
        }

        logf_info("Session resumed successfully.");
        session_resumed = true;
    }

    // On resume, authorization was already done before the pause: continue straight
    // into charge-parameter discovery. Each successor builds its own request in enter().
    if (session_resumed) {
        return {m_ctx.create_state<DC_ChargeParameterDiscovery>()};
    }

    return {m_ctx.create_state<AuthorizationSetup>()};
}

} // namespace iso15118::ev::d20::state
