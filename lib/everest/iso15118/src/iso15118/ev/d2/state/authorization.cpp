// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/authorization.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/crypto.hpp>
#include <iso15118/ev/detail/d2/state/authorization.hpp>

namespace iso15118::ev::d2::state {

namespace authorization {

message_2::AuthorizationRequest create_request() {
    return {};
}

message_2::AuthorizationRequest create_pnc_request(const dt::GenChallenge& gen_challenge) {
    message_2::AuthorizationRequest req;
    req.id = "id1";
    req.gen_challenge = gen_challenge;
    return req;
}

} // namespace authorization

bool Authorization::send() {
    if (not m_ctx.pnc.contract_selected) {
        m_ctx.send_request(authorization::create_request());
        return true;
    }

    auto req = authorization::create_pnc_request(m_ctx.pnc.gen_challenge);
    req.header.session_id = m_ctx.get_session_id();
    const crypto::PrivateKey key{m_ctx.pnc.contract_key_pem, m_ctx.pnc.contract_key_password};
    auto signed_exi = crypto::serialize_signed(req, key);
    if (signed_exi.empty()) {
        logf_error("Authorization: failed to sign the Plug & Charge AuthorizationReq; stopping the session");
        m_ctx.stop_session();
        return false;
    }
    m_ctx.send_raw(std::move(signed_exi), message_2::Type::AuthorizationReq);
    return true;
}

void Authorization::enter() {
    logf_debug("Enter state: Authorization (ISO 15118-2)");
    send();
}

Result Authorization::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    const auto* res = expect_response<message_2::AuthorizationResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    if (res->evse_processing != dt::EVSEProcessing::Finished) {
        if (not send()) {
            return Result::stopping();
        }
        return Result::awaiting();
    }

    return m_ctx.create_state<ChargeParameterDiscovery>();
}

} // namespace iso15118::ev::d2::state
