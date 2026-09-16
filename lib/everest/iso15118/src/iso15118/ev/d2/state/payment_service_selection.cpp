// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/payment_service_selection.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/authorization.hpp>
#include <iso15118/ev/d2/state/certificate_installation.hpp>
#include <iso15118/ev/d2/state/payment_details.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/payment_service_selection.hpp>

namespace iso15118::ev::d2::state {

namespace payment_service_selection {

message_2::PaymentServiceSelectionRequest create_request(uint16_t charge_service_id, dt::PaymentOption payment_option,
                                                         bool add_certificate_service) {
    message_2::PaymentServiceSelectionRequest req;
    req.selected_payment_option = payment_option;
    req.selected_service_list.push_back(dt::SelectedService{charge_service_id, std::nullopt});
    if (add_certificate_service) {
        req.selected_service_list.push_back(dt::SelectedService{dt::CERTIFICATE_SERVICE_ID, std::nullopt});
    }
    return req;
}

} // namespace payment_service_selection

namespace {

// enforce_contract selects Contract regardless of the SECC offer ([V2G2-135] robustness test).
bool use_contract(const Context& ctx) {
    const auto& pnc = ctx.params().pnc;
    return pnc.enforce_contract or (pnc.prefer_contract and ctx.evse_info.contract_offered);
}

bool install_cert(const Context& ctx) {
    return use_contract(ctx) and ctx.params().pnc.needs_cert_install() and ctx.evse_info.certificate_service_offered;
}

} // namespace

void PaymentServiceSelection::enter() {
    logf_debug("Enter state: PaymentServiceSelection (ISO 15118-2)");
    bool contract = use_contract(m_ctx);
    const auto& cfg = m_ctx.params().pnc;
    // Contract without a certificate to install and without a pre-installed one has nothing to
    // authenticate with; enforce_contract keeps the [V2G2-135] robustness case selectable.
    if (contract and not cfg.enforce_contract and not install_cert(m_ctx) and not cfg.has_contract_cert()) {
        logf_warning("Contract selected without an installable or pre-installed contract certificate; "
                     "falling back to ExternalPayment");
        contract = false;
    }
    m_ctx.pnc.contract_selected = contract;
    const auto option = contract ? dt::PaymentOption::Contract : dt::PaymentOption::ExternalPayment;
    m_ctx.send_request(payment_service_selection::create_request(m_ctx.evse_info.selected_charge_service_id, option,
                                                                 install_cert(m_ctx)));
}

Result PaymentServiceSelection::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    if (expect_response<message_2::PaymentServiceSelectionResponse>(m_ctx, *variant) == nullptr) {
        return Result::stopping();
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    if (not m_ctx.pnc.contract_selected) {
        return m_ctx.create_state<Authorization>();
    }

    if (install_cert(m_ctx)) {
        return m_ctx.create_state<CertificateInstallation>();
    }

    // Pre-installed contract: seed the runtime PnC state from the config.
    const auto& cfg = m_ctx.params().pnc;
    if (not cfg.has_contract_cert()) {
        // enforce_contract only: the SECC is expected to reject the empty PaymentDetailsReq.
        logf_warning("Contract enforced without a contract certificate; sending empty PaymentDetails material");
    }
    m_ctx.pnc.contract_cert_der = cfg.contract_cert_der;
    m_ctx.pnc.contract_sub_certs_der = cfg.contract_sub_certs_der;
    m_ctx.pnc.contract_key_pem = cfg.contract_key_pem;
    m_ctx.pnc.contract_key_password = cfg.contract_key_password;
    m_ctx.pnc.emaid = cfg.contract_emaid;
    return m_ctx.create_state<PaymentDetails>();
}

} // namespace iso15118::ev::d2::state
