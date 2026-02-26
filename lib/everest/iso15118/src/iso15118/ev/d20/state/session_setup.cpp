// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH, Roger Bedell, and Contributors to EVerest
#include <algorithm>
#include <iomanip>
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/authorization_setup.hpp>
#include <iso15118/ev/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/session_setup.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/io/sha_hash.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/session_setup.hpp>
#include <openssl/evp.h>
#include <optional>
#include <sstream>

namespace iso15118::ev::d20::state {

namespace {
using ResponseCode = message_20::datatypes::ResponseCode;
bool check_response_code(ResponseCode response_code) {
    switch (response_code) {
    case ResponseCode::OK_NewSessionEstablished:
        return true;
    case ResponseCode::OK_OldSessionJoined:
        return true;
    default:
        logf_warning("Unexpected response code received: %d", static_cast<int>(response_code));
        return iso15118::ev::d20::check_response_code(response_code);
    }
}

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
    // TODO(SL): Adding logging
}

Result SessionSetup::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();
    bool session_resumed = false;

    if (const auto res = variant->get_if<message_20::SessionSetupResponse>()) {

        if (not check_response_code(res->response_code)) {
            m_ctx.stop_session(true); // Tell stack to close the tcp/tls connection
            return {};
        }

        if (res->evseid.size() <= 0) {
            logf_error("EVSEID is empty. Abort the session.");
            m_ctx.stop_session(true); // Tell stack to close the tcp/tls connection
            return {};
        } else {
            // m_ctx.evse_info.evse_id = res->evseid;
        }

        // TODO(RB): Check if the returned  sessionid is ok by checking against the sent one.
        // If the sent one was 0, the new one should be non zero.
        const auto charger_cert_hash = m_ctx.get_charger_cert_hash();

        // Handle new session establishment or session resumption by checking the response_code from the EVSE
        if (res->response_code == message_20::datatypes::ResponseCode::OK_NewSessionEstablished) {
            logf_info("New session established by EVSE.");

            if (session_is_zero(res->header.session_id)) {
                logf_error("Returned SessionID is zero although a new session was requested. Abort the session.");
                m_ctx.stop_session(true); // Tell stack to close the tcp/tls connection
                return {};
            } else {
                // New session established successfully
                m_ctx.get_session().set_id(res->header.session_id);

                // Save the charger cert hash and the calculated hash of charger cert and session id for later use
                if (charger_cert_hash.has_value()) {
                    m_ctx.set_charger_cert_hash(charger_cert_hash.value());
                    const auto new_charger_cert_session_hash =
                        calculate_new_cert_session_id_hash(charger_cert_hash.value(), res->header.session_id);
                    m_ctx.set_charger_cert_session_hash(new_charger_cert_session_hash);
                } else {
                    logf_warning("No charger certificate hash available although a new session was established.");
                }
            }
        } else if (res->response_code == message_20::datatypes::ResponseCode::OK_OldSessionJoined) {
            // If the sent SessionID was non 0, the returned one should be the same. In order to
            // proceed it needs to be verified that the pairing of charger and ev is the same as before by checking a
            // hash of the charger cert and the session id that was saved when the session was paused with the new hash
            // calculated from the returned session id and the charger cert hash. If they are not the same, this is an
            // error, and the session should be shut down.
            // Make sure we have a charger cert hash to check against
            if (not charger_cert_hash.has_value()) {
                logf_error(
                    "No charger certificate hash available although an old session was resumed. Abort the session.");
                m_ctx.stop_session(true); // Tell stack to close the tcp/tls connection
                return {};
            }
            const auto new_charger_cert_session_hash =
                calculate_new_cert_session_id_hash(charger_cert_hash.value(), res->header.session_id);
            if (m_ctx.get_charger_cert_session_hash() != new_charger_cert_session_hash) {
                logf_error("Charger certificate/session hash does not match the saved one although an old session was "
                           "resumed. Abort the session.");
                m_ctx.stop_session(true); // Tell stack to close the tcp/tls connection
                return {};
            }
            // If we reach this point, the session has been successfully resumed
            logf_info("Session resumed successfully.");

            session_resumed = true;
        }

        // if the session is resumed, then no authorization setup or authorization needs to be done, we continue with
        // what was already in progress in the context.
        if (session_resumed) {
            logf_info("Session is resumed, continuing with the existing context.");
            // TODO(RB) Go directly to either AC or DC ChargeParameterDiscovery
            message_20::DC_ChargeParameterDiscoveryRequest req;
            setup_header(req.header, m_ctx.get_session());
            m_ctx.respond(req);
            // The DC_ChargeParameterDiscovery state handles the DC_ChargeParameterDiscoveryResponse
            return {m_ctx.create_state<DC_ChargeParameterDiscovery>()};
        } else {
            message_20::AuthorizationSetupRequest req;
            setup_header(req.header, m_ctx.get_session());
            m_ctx.respond(req);
            return {m_ctx.create_state<AuthorizationSetup>()};
        }
    } else {
        logf_error("expected SessionSetupResponse! But code type id: %d", variant->get_type());
        m_ctx.stop_session(true); // Tell stack to close the tcp/tls connection
        return {};
    }
}

} // namespace iso15118::ev::d20::state
