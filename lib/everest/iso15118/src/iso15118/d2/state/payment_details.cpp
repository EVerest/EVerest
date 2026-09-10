// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/payment_details.hpp>

#include <ctime>

#include <iso15118/d2/state/authorization.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/crypto.hpp>
#include <iso15118/detail/d2/state/payment_details.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/detail/random.hpp>

namespace iso15118::d2::state {

dt::GenChallenge generate_gen_challenge() {
    // [V2G2-826]/[V2G2-835]: GenChallenge must be cryptographically random.
    dt::GenChallenge challenge{};
    fill_random(challenge.data(), challenge.size());
    return challenge;
}

void PaymentDetails::enter() {
    logf_debug("Enter state: PaymentDetails");
}

Result PaymentDetails::on_request(const message_2::Variant& received) {
    // [V2G2-554]/[V2G2-557]/[V2G2-558]: the optional certificate exchange has been used up.
    const auto type = received.get_type();
    if (type == message_2::Type::PaymentDetailsReq) {
        return process_payment_details(m_ctx, received.get<message_2::PaymentDetailsRequest>());
    } else {
        logf_warning("Expected PaymentDetailsReq! But got type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

Result process_payment_details(Context& m_ctx, const message_2::PaymentDetailsRequest& req) {
    message_2::PaymentDetailsResponse res;
    res.header.session_id = m_ctx.get_session_id();
    res.evse_timestamp = static_cast<int64_t>(std::time(nullptr));

    const auto validation = crypto::validate_contract_chain(req.contract_certificate, req.sub_certificates, req.emaid,
                                                            m_ctx.session_config.mo_root_cert_path,
                                                            m_ctx.session_config.v2g_root_cert_path);

    res.response_code = validation.response_code;

    // Mandatory in the response even on failure. Generated here and checked in exactly one place -- the
    // AuthorizationReq that has to echo it [V2G2-475] -- so it travels by construction, not via the session.
    const auto gen_challenge = generate_gen_challenge();
    res.gen_challenge = gen_challenge;

    if (validation.response_code != dt::ResponseCode::OK) {
        // A missing local trust anchor is not fatal when central contract validation is allowed: forward the
        // chain to the CSMS, which validates it in the PnC Authorize instead. Deliberate deviation from
        // [V2G2-899], covering the OCPP 2.0.1 C07.FR.06 deployment where the CPO installs no MO root.
        if (validation.forwardable and m_ctx.session_config.central_contract_validation_allowed) {
            logf_warning("PaymentDetails: local contract chain validation not possible, forwarding the "
                         "contract for central validation");
            res.response_code = dt::ResponseCode::OK;
        } else {
            logf_warning("PaymentDetails: contract chain validation failed");
            m_ctx.respond(res);
            m_ctx.session_stopped = true;
            return {};
        }
    }

    m_ctx.set_contract_identity(req.contract_certificate, validation.emaid, validation.chain_pem);

    m_ctx.respond(res);

    return m_ctx.create_state<Authorization>(gen_challenge);
}

} // namespace iso15118::d2::state
