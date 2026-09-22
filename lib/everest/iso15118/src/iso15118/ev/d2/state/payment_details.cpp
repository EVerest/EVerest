// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/payment_details.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/authorization.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/payment_details.hpp>

namespace iso15118::ev::d2::state {

namespace payment_details {

message_2::PaymentDetailsRequest create_request(const ContractMaterial& contract) {
    message_2::PaymentDetailsRequest req;
    req.emaid = contract.emaid;
    req.contract_certificate = contract.cert_der;
    req.sub_certificates = contract.sub_certs_der;
    return req;
}

} // namespace payment_details

BasePointerType to_payment_details(Context& ctx) {
    // Without a contract (enforce_contract only) the request carries empty material.
    ctx.send_request(payment_details::create_request(ctx.pnc.contract ? *ctx.pnc.contract : ContractMaterial{}));
    return ctx.create_state<PaymentDetails>();
}

void PaymentDetails::enter() {
    logf_debug("Enter state: PaymentDetails (ISO 15118-2)");
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

    // Echoed and signed over in the AuthorizationReq [V2G2-900].
    m_ctx.pnc.gen_challenge = res->gen_challenge;

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }
    return to_authorization(m_ctx);
}

} // namespace iso15118::ev::d2::state
