// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/payment_details.hpp>

#include <ctime>
#include <random>

#include <iso15118/d2/state/authorization.hpp>
#include <iso15118/d2/state/certificate_installation.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/crypto.hpp>
#include <iso15118/detail/d2/state/payment_details.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

dt::GenChallenge generate_gen_challenge() {
    dt::GenChallenge challenge{};
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> distribution(0x00, 0xff);
    for (auto& byte : challenge) {
        byte = static_cast<uint8_t>(distribution(generator));
    }
    return challenge;
}

void PaymentDetails::enter() {
    m_ctx.log.enter_state("PaymentDetails");
}

Result PaymentDetails::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting mid-handshake sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_2::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    // Plug-and-Charge: after PaymentServiceSelection(Contract) the EV may run a CertificateInstallation
    // or CertificateUpdate before PaymentDetails. Relay it (raw pass-through) via the same state, then
    // return here for the PaymentDetailsReq.
    if (m_ctx.peek_request_type() == message_2::Type::CertificateInstallationReq or
        m_ctx.peek_request_type() == message_2::Type::CertificateUpdateReq) {
        return m_ctx.create_state<CertificateInstallation>();
    }

    const auto variant = m_ctx.pull_request();

    const auto req = variant->get_if<message_2::PaymentDetailsRequest>();
    if (req == nullptr) {
        m_ctx.log("expected PaymentDetailsReq! But code type id: %d", variant->get_type());
        // [V2G2-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
        respond_sequence_error(m_ctx, *variant);
        m_ctx.session_stopped = true;
        return {};
    }

    // The request must echo the assigned SessionID [V2G2-388]; a mismatch is answered with the
    // received-type response carrying FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    message_2::PaymentDetailsResponse res;
    res.header.session_id = m_ctx.get_session_id();
    res.evse_timestamp = static_cast<int64_t>(std::time(nullptr));

    const auto validation =
        crypto::validate_contract_chain(req->contract_certificate, req->sub_certificates, req->emaid,
                                        m_ctx.session_config.mo_root_cert_path, m_ctx.session_config.v2g_root_cert_path);

    res.response_code = validation.response_code;

    // GenChallenge is mandatory in the response even on failure (fixed 16-byte size).
    m_ctx.gen_challenge = generate_gen_challenge();
    res.gen_challenge = m_ctx.gen_challenge;

    if (validation.response_code != dt::ResponseCode::OK) {
        m_ctx.log("PaymentDetails: contract chain validation failed");
        m_ctx.respond(res);
        m_ctx.session_stopped = true;
        return {};
    }

    // Store the contract identity for the Authorization signature check and the require_auth_pnc token.
    m_ctx.contract_leaf_der = req->contract_certificate;
    m_ctx.contract_emaid = validation.emaid;
    m_ctx.contract_chain_pem = validation.chain_pem;

    m_ctx.respond(res);

    return m_ctx.create_state<Authorization>();
}

} // namespace iso15118::d2::state
