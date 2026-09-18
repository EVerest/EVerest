// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/payment_details.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/authorization.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/message_2/payment_details.hpp>

namespace iso15118::ev::d2::state {

void PaymentDetails::enter() {
    logf_debug("Enter state: PaymentDetails (ISO 15118-2)");
    message_2::PaymentDetailsRequest req;
    req.emaid = m_ctx.pnc.emaid;
    req.contract_certificate = m_ctx.pnc.contract_cert_der;
    req.sub_certificates = m_ctx.pnc.contract_sub_certs_der;
    m_ctx.send_request(req);
}

Result PaymentDetails::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    const auto* res = expect_response<message_2::PaymentDetailsResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    // Echoed and signed over in the AuthorizationReq [V2G2-684].
    m_ctx.pnc.gen_challenge = res->gen_challenge;

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }
    return m_ctx.create_state<Authorization>();
}

} // namespace iso15118::ev::d2::state
